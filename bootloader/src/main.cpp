#define _GNU_SOURCE
#include <string.h>

#include "mbed.h"
#include "bhy_upload.h"
#include "SPIFBlockDevice.h"
#include "IS31FL3194.h"
#include "LittleFileSystem.h"
#include "file_utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#define MOUNT_PATH           "fs"
#define ANNA_UPDATE_FILE_PATH   "/" MOUNT_PATH "/ANNA_UPDATE.BIN"
#define FAIL_SAFE_FILE_PATH "/" MOUNT_PATH "/FAIL_SAFE.BIN"

#define DEBUG_PRINTF(...) printf(__VA_ARGS__)

#define WRONG_OTA_BINARY 	(-1)
#define MOUNT_FAILED 		(-2)
#define NO_OTA_FILE 		(-3)
#define INIT_FAILED 		(-4)

#if !defined(POST_APPLICATION_ADDR)
#error "target.restrict_size must be set for your target in mbed_app.json"
#endif

//Pin order: MOSI, MISO, SCK, CS
SPIFBlockDevice spif(MBED_CONF_APP_SPIF_MOSI, MBED_CONF_APP_SPIF_MISO,
                     MBED_CONF_APP_SPIF_SCK, MBED_CONF_APP_SPIF_CS);

LittleFileSystem fs(MOUNT_PATH);
FlashIAP flash;

IS31FL3194 leds;

FileUtils files;

DigitalIn* boot_rst_n;
Timer timer_rst_n;

int try_fail_safe(int timeout);
int try_update(void);
int apply_update(FILE *file, uint32_t address, bool fail_safe);


int check_signature(bool fail_safe)
{
    void *signature = NULL;
    signature = memmem((const char*)POST_APPLICATION_ADDR, POST_APPLICATION_SIZE, "UNISENSE", sizeof("UNISENSE"));

    if (signature != NULL) {
        //Signature found: fw can be executed
        printf("Signature check PASSED \r\n");
        printf("Starting application\r\n");
        mbed_start_application(POST_APPLICATION_ADDR);
    } else {
        //Signature NOT found: do not run the current fw and try fail safe
        printf("Unisense signature NOT found! \r\n");
        if (fail_safe) {
            return 0;
        } else {
            printf("Press the button for at least 3 seconds to enter the Fail Safe mode \r\n");
            //wait for the button to be pressed
            while(*boot_rst_n) {}
            if(try_fail_safe(3000)) {
                printf("Starting safe application\r\n");
                mbed_start_application(POST_APPLICATION_ADDR);
            } else {
                return 0;
            }
        }
    }
}


int try_fail_safe(int timeout)
{
    //Try fail safe: check button state
    //if button pressed, start timer
    printf("Button pressed \r\n");
    int elapsed_time_ms = 0;
    timer_rst_n.start();

    while((elapsed_time_ms < timeout) && (*boot_rst_n == 0)) {
        elapsed_time_ms = timer_rst_n.read_ms();
    }

    timer_rst_n.stop();
    timer_rst_n.reset();

    if (elapsed_time_ms < timeout) {
        //NO fail safe
        printf("Button for fail safe has been pulled down for less than 3 seconds. No fail safe! \r\n");
        return 0;
    } else {
        //Fail safe
        printf("Fail safe mode ON \r\n");
        //Retrieve a know firmware from FAIL_SAFE.BIN file
        FILE *file_fail_safe = fopen(FAIL_SAFE_FILE_PATH, "rb");
        if (file_fail_safe != NULL) {
            printf("Fail safe file found \r\n");

            int safe_fw_found = apply_update(file_fail_safe, POST_APPLICATION_ADDR, true);

            fclose(file_fail_safe);

            return safe_fw_found;

        } else {
            printf("No fail safe firmware found \r\n");
            return 0;
        }
    }
}


int apply_update(FILE *file, uint32_t address, bool fail_safe)
{
    long len = files.getFileLen(file);

    if (len == 0) {
        //No valid firmware update found, the main application can start
        return 1;
    }

    printf("Firmware size is %ld bytes\r\n", len);

    char crc_file = files.getFileCRC(file);
    printf("CRC written in file is %x \r\n", crc_file);

    fseek(file, 0, SEEK_SET);

    char crc = files.computeCRC(file);

    if (crc!=crc_file) {
        if (!fail_safe) {
            printf("Wrong CRC! The computed CRC is %x, while it should be %x \r\n", crc, crc_file);
            printf("Press the button for at least 3 seconds to enter the Fail Safe mode \r\n");

            //wait for the button to be pressed
            while(*boot_rst_n) {}

            return try_fail_safe(3000);
        } else {
            printf("ERROR! Wrong CRC in fail safe sketch \r\n");
            return 0;
        }

    } else {
        printf("Correct CRC=%x \r\n", crc);
    }

    fseek(file, 0, SEEK_SET);
  
    flash.init();

    const uint32_t page_size = flash.get_page_size();
    char *page_buffer = new char[page_size];
    uint32_t addr = address;
    uint32_t next_sector = addr + flash.get_sector_size(addr);
    bool sector_erased = false;
    size_t pages_flashed = 0;
    uint32_t percent_done = 0;
    while (true) {

        // Read data for this page
        memset(page_buffer, 0, sizeof(char) * page_size);
        int size_read = fread(page_buffer, 1, page_size, file);
        if (size_read <= 0) {
            break;
        }

        // Erase this page if it hasn't been erased
        if (!sector_erased) {
            flash.erase(addr, flash.get_sector_size(addr));
            sector_erased = true;
        }

        // Program page
        flash.program(page_buffer, addr, page_size);

        addr += page_size;
        if (addr >= next_sector) {
            next_sector = addr + flash.get_sector_size(addr);
            sector_erased = false;
        }

        if (++pages_flashed % 3 == 0) {
            uint32_t percent_done_new = ftell(file) * 100 / len;
            if (percent_done != percent_done_new) {
                percent_done = percent_done_new;
            }
        }
    }
    printf("Flashed 100%%\r\n");

    delete[] page_buffer;

    flash.deinit();

    return 1;
}


int try_update()
{
    int update = 0;

    FILE *file = fopen(ANNA_UPDATE_FILE_PATH, "rb");
    if (file != NULL) {

        update = apply_update(file, POST_APPLICATION_ADDR, false);

        fclose(file);
        remove(ANNA_UPDATE_FILE_PATH);

    } else {
        printf("No ANNA_UPDATE_FILE found. Starting main application\r\n");
        mbed_start_application(POST_APPLICATION_ADDR);
    }

    if (update) {
        check_signature(false);
    }
}

int main()
{

    CoreDebug->DEMCR = 0;
    NRF_CLOCK->TRACECONFIG = 0;

    boot_rst_n = new DigitalIn(BUTTON1, PullUp);

    bool forced_reboot = !*boot_rst_n;
    printf("Bootloader starting\r\n");

    //Mount the file system
    int err = fs.mount(&spif);
    if (err) {
        DEBUG_PRINTF("Formatting file system...\n");
        err=fs.reformat(&spif);
        if (err) {
            DEBUG_PRINTF("Mount failed\n");
            return MOUNT_FAILED;
        }
    }

    printf("IS31FL3194 RGB led driver %02X\n", leds.getChipID());

    leds.reset();
    leds.init();
    leds.powerUp();

    fwupdate_bhi260();
    /*
    while (1) {
        leds.ledBlink(green, 1000);
        ThisThread::sleep_for(1s);
    }
    */
    if(forced_reboot) {

        if(try_fail_safe(3000)) {
            check_signature(true);
        }

    } else {

        try_update();

    }

    fs.unmount();
    spif.deinit();
}