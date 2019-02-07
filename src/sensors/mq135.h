#include "sensor.h"

class MQ135Sensor : public Sensor
{
public:
    // std::string name
    // int GAS_PIN = 19   /* GPIO-19 */
    MQ135Sensor(const std::string& name, int GAS_PIN);
    ~MQ135Sensor();

    // update sensor, read data from sensor
    void update() override;

private:
    // GPIO pin
    int GAS_PIN_;
};
