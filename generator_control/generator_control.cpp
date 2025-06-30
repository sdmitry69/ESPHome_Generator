#include "generator_control.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h" // Добавляем этот заголовок для доступа к millis()

namespace esphome {
namespace generator_control {

static const char *TAG = "generator_control";

void GeneratorControl::setup() {
  // Выключаем все реле при старте
  for (auto relay : this->relays_) {
    relay->turn_off();
  }
  
  this->set_output_value(GC_VAL_REGIME, (float)this->current_regime_);
  this->set_output_value(GC_VAL_REGSTEP, (float)this->current_step_);
  this->set_output_value(GC_VAL_TIMEOUT, (float)0 );
}

void GeneratorControl::loop() {
  // Проверяем состояние управляющего переключателя
  if (this->control_switch_ == nullptr)
    return;
    
  bool current_control_state = this->control_switch_->state;
  bool current_control_ac = this->control_ac_->state;
  
  // Если поступила команда на изменение режима работы
  if (current_control_state != this->last_control_state_) {
    this->last_control_state_ = current_control_state;
    this->last_step_time_ = 0; // сброс задержки выполнения при нажатии клавиши
    this->twait_ = 0;
    
    if (current_control_state) {
      // Переключатель включен - запускаем последовательность
      this->start_sequence();
    } else {
      // Переключатель выключен - останавливаем последовательность
      this->stop_sequence();
    }
  }
  
  // Если поступила информация о состоянии сети 220v
  if (current_control_ac != this->last_control_ac_) {
    this->last_control_ac_ = current_control_ac;

    if (current_control_ac) 
    { // Флаг установлен - запускаем последовательность нормального напряжения
      this->start_sequence_ac_ok();
    } else 
    {
      // Флаг сброшен - запускаем последовательность выключения генератора 
      this->start_sequence_ac_fail();
    }
  }
  
  
  // Если последовательность запущена, проверяем время для следующего шага
  if (this->sequence_running_ && millis() - this->last_step_time_ >= 500) 
  {
    if( this->twait_<millis() )
    {
        this->sequence_step(this->current_regime_, this->current_step_);
        this->last_step_time_ = millis();
        if( this->val_timeout!=0 )
        {
          this->set_output_value(GC_VAL_TIMEOUT, (float)0 );
          this->val_timeout=0;
        }
    }
    else
    {
      if( this->val_timeout!=((int)(this->twait_-millis())/1000) )
      {
        this->val_timeout=((int)(this->twait_-millis())/1000);
        this->set_output_value(GC_VAL_TIMEOUT, (float)(this->val_timeout) );  
      }
    }
  }
}

// Метод для программного нажатия кнопки
void GeneratorControl::press_button(size_t index) {
  if (index < this->buttons_.size() && this->buttons_[index] != nullptr) {
    ESP_LOGI(TAG, "Программное нажатие кнопки %d", index);
    this->buttons_[index]->press();
  } else {
    ESP_LOGW(TAG, "Попытка нажать несуществующую кнопку с индексом %d", index);
  }
}

void GeneratorControl::start_sequence() {
  ESP_LOGI(TAG, "Запуск генератора");
  this->sequence_running_ = true;
  this->sequence_set(GC_REGIME_START, GC_STEP_START_BEGIN);
  this->last_step_time_ = millis();
  this->restart=0;
  this->sequence_step(this->current_regime_, this->current_step_);
}

void GeneratorControl::stop_sequence() {
  ESP_LOGI(TAG, "Остановка генератора");
  this->sequence_running_ = true;
  this->sequence_set(GC_REGIME_STOP, GC_STEP_STOP_BEGIN);
  this->last_step_time_ = millis();
  this->sequence_step(this->current_regime_, this->current_step_);
}

void GeneratorControl::start_sequence_ac_ok() 
{
  if( this->current_regime_!=GC_REGIME_STOP )   
  {
      ESP_LOGI(TAG, "Выключение генератора");
      this->sequence_running_ = true;
      this->sequence_set(GC_REGIME_AC_OK, GC_STEP_AC_BEGIN);
      this->last_step_time_ = millis();
      this->restart=0;
      this->sequence_step(this->current_regime_, this->current_step_);
  }
}

void GeneratorControl::start_sequence_ac_fail() {
  if( this->current_regime_!=GC_REGIME_START )   
  {
      ESP_LOGI(TAG, "Включение генератора");
      this->sequence_running_ = true;
      this->sequence_set(GC_REGIME_AC_FAIL, GC_STEP_AC_BEGIN);
      this->last_step_time_ = millis();
      this->restart=0;
      this->sequence_step(this->current_regime_, this->current_step_);
  }
}

void GeneratorControl::sequence_step(int reg, int step) {

  switch( reg )
  {
      case GC_REGIME_STOP: 
        this->sequence_stop( step );
        break;
        
      case GC_REGIME_START:
        this->sequence_start( step );
        break;
        
      case GC_REGIME_AC_OK: 
        this->sequence_ac_ok( step );
        break;
      
      case GC_REGIME_AC_FAIL: 
        this->sequence_ac_fail( step );
        break;
  }
}

void GeneratorControl::sequence_ac_ok(int step) 
{
  switch( step )
  {
      case GC_STEP_AC_BEGIN: 
        sequence_setdelay(15*60*1000);  // 15 минут
        this->sequence_setstep( GC_STEP_START_AC_GEN_OFF );
        break;
        
      case GC_STEP_START_AC_GEN_OFF: 
        this->sequence_set(GC_REGIME_STOP, GC_STEP_STOP_BEGIN);
        break;
      
      default:
        this->sequence_set(GC_REGIME_STOP, GC_STEP_STOP_BEGIN);
        break;
  }
}

void GeneratorControl::sequence_ac_fail(int step) 
{
  switch( step )
  {
      case GC_STEP_AC_BEGIN: 
        sequence_setdelay(30*60*1000);  // 30 минут
        this->sequence_setstep( GC_STEP_START_AC_GEN_ON );
        break;
        
      case GC_STEP_START_AC_GEN_ON: 
        this->sequence_set(GC_REGIME_START, GC_STEP_START_BEGIN);
        break;
      
      default:
        this->sequence_set(GC_REGIME_STOP, GC_STEP_STOP_BEGIN);
        break;
  }
}

void GeneratorControl::sequence_set(int reg, int step)
{
    this->current_regime_ = reg;
    this->current_step_   = step;
    this->set_output_value(GC_VAL_REGIME, (float)this->current_regime_);
    this->set_output_value(GC_VAL_REGSTEP, (float)this->current_step_);
}
void GeneratorControl::sequence_setstep(int step)
{
    this->current_step_   = step;
    this->set_output_value(GC_VAL_REGSTEP, (float)this->current_step_);
}

void GeneratorControl::sequence_setdelay(uint32_t twait)
{
    this->twait_ = millis()+twait;
}

void GeneratorControl::sequence_start(int step) 
{
  switch( step )
  {
      case GC_STEP_START_BEGIN: 
        if( this->get_analog_value(GC_ADC_AI3)>10 ) this->sequence_setstep( GC_STEP_START_POWER_ON );
        else                                        this->sequence_setstep( GC_STEP_START_ENGINE_ON );
        break;
        
      case GC_STEP_START_ENGINE_ON: 
        this->relays_[GC_RELAY_FUEL]->turn_on();
        this->relays_[GC_RELAY_ENGINE]->turn_on();
         
        this->sequence_setstep( GC_STEP_START_AIRCLOSE );
        sequence_setdelay(5000); // задержка поступления топлива
        break;
        
      case GC_STEP_START_AIRCLOSE:
        if( !this->get_binary_value(GC_IN5) )
        {
            this->buttons_[GC_BUTTON_AIRCLOSE]->press();
        }
        this->sequence_setstep( GC_STEP_START_STARTER_ON );
        break;
        
      case GC_STEP_START_STARTER_ON:
        this->twaitcmd = millis()+15000; // 15 секунд время запуска генератора
        this->relays_[GC_RELAY_STARTER]->turn_on();
        this->sequence_setstep( GC_STEP_START_STARTER_WAIT );
        break;
        
      case GC_STEP_START_STARTER_WAIT:
        if( this->twaitcmd<millis() || this->get_analog_value(GC_ADC_AI3)>10 )
        {
          this->sequence_setstep( GC_STEP_START_STARTER_STOP );  
        }
        break;
        
      case GC_STEP_START_STARTER_STOP:
        this->relays_[GC_RELAY_STARTER]->turn_off();
        if( this->get_analog_value(GC_ADC_AI3)>10 ) // генератор завелся
        {
          sequence_setdelay(3000);  // задержка открытия заслонки
          this->sequence_setstep( GC_STEP_START_AIROPEN );
        }
        else // генератор не завелся
        {
          sequence_setdelay(15000); // пауза для повторного запуска
          this->sequence_setstep( GC_STEP_START_WAIT_RESTART );
        }
        break;
        
      case GC_STEP_START_WAIT_RESTART:
        // через раз перезапускаем то с открытой, то с закрытой заслонкой
        if( this->restart > 6 ) // закончились попытки запуска
        {
           this->sequence_set(GC_REGIME_STOP, GC_STEP_STOP_BEGIN); 
        }
        else
        {
            if( this->restart%2==0 ) 
            {
                this->buttons_[GC_BUTTON_AIROPEN]->press();
                this->sequence_setstep( GC_STEP_START_STARTER_ON );
            }
            else
            {
               this->sequence_setstep( GC_STEP_START_AIRCLOSE ); 
            }
        }
        this->restart++;
        break;
        
      case GC_STEP_START_AIROPEN:
        this->buttons_[GC_BUTTON_AIROPEN]->press();
        sequence_setdelay(30000);  // подключения нагрузки
        this->sequence_setstep( GC_STEP_START_POWER_ON );
        break;
        
      case GC_STEP_START_POWER_ON:
        this->relays_[GC_RELAY_POWER]->turn_on();
        this->sequence_setstep( GC_STEP_START_END );
        break;
        
      case GC_STEP_START_END:
        break;
        
      default:
        this->sequence_set(GC_REGIME_NULL, GC_STEP_NULL);
        break;
  }
}
  
void GeneratorControl::sequence_stop(int step) 
{
  switch( step )
  {
      case GC_STEP_STOP_BEGIN: 
        if( this->relays_[GC_RELAY_POWER]->state ) this->sequence_setstep( GC_STEP_STOP_POWER_OFF );
        else                                       this->sequence_setstep( GC_STEP_STOP_ENGINE_OFF );
        break;
        
      case GC_STEP_STOP_POWER_OFF: 
        this->relays_[GC_RELAY_POWER]->turn_off();
        sequence_setdelay(20000); // задержка перед выключением генера
        this->sequence_setstep( GC_STEP_STOP_ENGINE_OFF );
        break;
        
      case GC_STEP_STOP_ENGINE_OFF:
        // Выключаем все реле
        for (auto relay : this->relays_) {
            relay->turn_off();
        }
        this->sequence_setstep( GC_STEP_STOP_END );
        break;
              
      case GC_STEP_STOP_END:
        break;
        
      default:
        this->sequence_set(GC_REGIME_NULL, GC_STEP_NULL);
        break;
        
  }
}

// Методы для получения значений датчиков
float GeneratorControl::get_analog_value(size_t index) const {
  if (index < this->analog_sensors_.size() && this->analog_sensors_[index] != nullptr) {
    return this->analog_sensors_[index]->state;
  }
  return 0.0f;
}

bool GeneratorControl::get_binary_value(size_t index) const {
  if (index < this->binary_sensors_.size() && this->binary_sensors_[index] != nullptr) {
    return this->binary_sensors_[index]->state;
  }
  return false;
}

float GeneratorControl::get_modbus_value(size_t index) const {
  if (index < this->modbus_sensors_.size() && this->modbus_sensors_[index] != nullptr) {
    return this->modbus_sensors_[index]->state;
  }
  return 0.0f;
}

// Метод для установки значения выходного датчика
void GeneratorControl::set_output_value(size_t index, float value) {
  if (index < this->output_sensors_.size() && this->output_sensors_[index] != nullptr) {
    this->output_sensors_[index]->publish_state(value);
  }
}


} // namespace generator_control
} // namespace esphome