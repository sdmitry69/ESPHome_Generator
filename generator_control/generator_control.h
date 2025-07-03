#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/button/button.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/hal.h" // Добавляем этот заголовок для доступа к milli
#include <vector>

#define GC_REGIME_NULL      0
#define GC_REGIME_STOP      1
#define GC_REGIME_START     2
#define GC_REGIME_AC_OK     3
#define GC_REGIME_AC_FAIL   4

#define GC_STEP_NULL        0

#define GC_STEP_START_BEGIN         1
#define GC_STEP_START_ENGINE_ON     2
#define GC_STEP_START_AIRCLOSE      3
#define GC_STEP_START_STARTER_ON    4
#define GC_STEP_START_STARTER_WAIT  5
#define GC_STEP_START_STARTER_STOP  6
#define GC_STEP_START_AIROPEN       7
#define GC_STEP_START_POWER_ON      8
#define GC_STEP_START_POWER_OFF     9
#define GC_STEP_START_STOP          10
#define GC_STEP_START_WAIT_RESTART  11
#define GC_STEP_START_END           99


#define GC_STEP_STOP_BEGIN          101
#define GC_STEP_STOP_POWER_OFF      102
#define GC_STEP_STOP_OFF            103
#define GC_STEP_STOP_ENGINE_OFF     104
#define GC_STEP_STOP_END            199

#define GC_STEP_AC_BEGIN            201
#define GC_STEP_START_AC_GEN_OFF    202
#define GC_STEP_START_AC_GEN_ON     203


#define GC_RELAY_FUEL               0
#define GC_RELAY_ENGINE             1
#define GC_RELAY_STARTER            2
#define GC_RELAY_AIR_TO_OFF         3
#define GC_RELAY_AIR_TO_ON          4
#define GC_RELAY_POWER              5

#define GC_BUTTON_AIRCLOSE          0
#define GC_BUTTON_AIROPEN           1

#define GC_ADC_AI1                  0
#define GC_ADC_AI2                  1
#define GC_ADC_AI3                  2
#define GC_ADC_AI4                  3

#define GC_IN1                      0
#define GC_IN2                      1
#define GC_IN3                      2
#define GC_IN4                      3
#define GC_IN5                      4
#define GC_IN6                      5
#define GC_IN_AC_CTRL               6

#define GC_VAL_REGIME               0
#define GC_VAL_REGSTEP              1
#define GC_VAL_TIMEOUT              2
#define GC_VAL_MOTOHR               3
#define GC_VAL_GAS                  4

extern int generator_motohr_eeprom;
extern int generator_gas_eeprom;


namespace esphome {
namespace generator_control {

class GeneratorControl : public Component {
 public:
  void setup() override;
  void loop() override;
  
  void set_control_switch(switch_::Switch *control_switch) { control_switch_ = control_switch; }
  void set_control_ac(switch_::Switch *control_ac) { control_ac_ = control_ac; }
  void add_relay(switch_::Switch *relay) { relays_.push_back(relay); }
  
  // Методы для добавления датчиков
  void add_analog_sensor(sensor::Sensor *sensor) { analog_sensors_.push_back(sensor); }
  void add_binary_sensor(binary_sensor::BinarySensor *sensor) { binary_sensors_.push_back(sensor); }
  void add_modbus_sensor(sensor::Sensor *sensor) { modbus_sensors_.push_back(sensor); }
  
  // Метод для добавления кнопок
  void add_button(button::Button *btn) { buttons_.push_back(btn); }
  
  // Метод для добавления выходных датчиков
  void add_output_sensor(sensor::Sensor *sensor) { output_sensors_.push_back(sensor); }
  
  // Метод для установки значения выходного датчика
  void set_output_value(size_t index, float value);
  
  // Метод для программного нажатия кнопки
  void press_button(size_t index);
  
  // Методы для получения значений датчиков
  float get_analog_value(size_t index) const;
  bool get_binary_value(size_t index) const;
  float get_modbus_value(size_t index) const;
  bool is_binary_valid(size_t index) const;

  void CheckMotoHrAndOil();
  uint32_t iTime() { return millis()/1000; }

 protected:
  void start_sequence();
  void stop_sequence();
  void start_sequence_ac_ok();
  void start_sequence_ac_fail();
  void sequence_step(int reg, int step);
  void sequence_set(int reg, int step);
  void sequence_setstep(int step);
  void sequence_setdelay(uint32_t twait);
  void sequence_stop(int step);
  void sequence_start(int step);
  void sequence_ac_ok(int step);
  void sequence_ac_fail(int step);

  ESPPreferenceObject  generator_motohr_eeprom;
  ESPPreferenceObject  generator_gas_eeprom;
  
  std::vector<sensor::Sensor *> analog_sensors_;
  std::vector<binary_sensor::BinarySensor *> binary_sensors_;
  std::vector<sensor::Sensor *> modbus_sensors_;
  std::vector<button::Button *> buttons_;
  switch_::Switch *control_switch_{nullptr};
  switch_::Switch *control_ac_{nullptr};
  std::vector<switch_::Switch *> relays_;
  std::vector<sensor::Sensor *> output_sensors_;  // датчики для вывода значений
  
  bool sequence_running_{false};
  bool last_control_state_{false};
  bool last_control_ac_{false};
  int current_step_{GC_STEP_NULL};
  int current_regime_{GC_REGIME_NULL};
  uint32_t last_step_time_{0};
  uint32_t twait_{0};
  uint32_t twaitcmd{0};
  uint32_t tsync_ha_flags{0};
  int restart{0};
  int val_timeout{0};

  uint32_t  tMotoHr{0};         // моточасы  
  uint32_t  tOilMin{30600};     // объем топлива в секундах полный бак на 8.5 часов 

  uint32_t  tEnginOnBegTime{0};
  bool      bEnginOn{false};
  uint32_t  tMotHrSave{0};
};

} // namespace generator_control
} // namespace esphome