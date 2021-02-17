#ifndef BOSCH_SENSORTEC_H_
#define BOSCH_SENSORTEC_H_

#include "Arduino.h"
#include "mbed.h"

#include "bosch/common/common.h"
#include "SensorTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif
#include "bosch/bhy2.h"
#ifdef __cplusplus
}
#endif

#define SENSOR_QUEUE_SIZE   10
#define WORK_BUFFER_SIZE    2048

#define MAX_READ_WRITE_LEN 256

class BoschSensortec {
public:
  BoschSensortec();
  virtual ~BoschSensortec();

  // sketch-side API
  void begin(); 
  void update();
  void configureSensor(SensorConfigurationPacket& config);

  uint8_t availableSensorData();
  bool readSensorData(SensorDataPacket &data);

  // ANNA <-> BOSCH interface
  void addSensorData(const SensorDataPacket &sensorData);

private:
  mbed::CircularBuffer<SensorDataPacket, SENSOR_QUEUE_SIZE, uint8_t> _sensorQueue;

  uint8_t _workBuffer[WORK_BUFFER_SIZE];

  SensorConfigurationPacket* _savedConfig;
  
  struct bhy2_dev _bhy2;

private:
  friend class Arduino_BHY2;
  void debug(Stream &stream);
  Stream *_debug;
};

extern BoschSensortec sensortec;

#endif