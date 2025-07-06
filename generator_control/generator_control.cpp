#include "generator_control.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h" // Добавляем этот заголовок для доступа к millis()
#include "esphome/core/preferences.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace generator_control {

static const char *TAG = "generator_control";
static uint32_t generator_counter = 0;

void GeneratorControl::setup() 
{
  // Выключаем все реле при старте
  for (auto relay : this->relays_) { relay->turn_off(); }
  
  this->set_output_value(GC_VAL_REGIME, (float)this->current_regime_);
  this->set_output_value(GC_VAL_REGSTEP, (float)this->current_step_);
  this->set_output_value(GC_VAL_TIMEOUT, (float)0 );
  this->tsync_ha_flags=millis()+30000; // опрос через 30 секунд

  // инициализация переменных  EEPROM
  this->generator_motohr_eeprom = global_preferences->make_preference<int>(++generator_counter ^ 0x1234567);
  this->generator_gas_eeprom = global_preferences->make_preference<int>(++generator_counter ^ 0x7654321);
  this->generator_total_power_eeprom = global_preferences->make_preference<int>(++generator_counter ^ 0x7654321);

  this->generator_motohr_eeprom.load(&this->tMotoHr);
  this->generator_gas_eeprom.load(&this->tOilMin);
  this->generator_total_power_eeprom.load(&this->nTotalPower);

  /* первичная установка значений
  if( this->tMotoHr==118*60 ) 
  {
    this->tMotoHr = 7090*60;    // установка значений из предыдущей конфигурации
    this->generator_motohr_eeprom.save(&this->tMotoHr);
  }

  if( this->tOilMin==510*60 ) 
  {
    this->tOilMin = 330*60;    // установка значений из предыдущей конфигурации
    this->generator_gas_eeprom.save(&this->tOilMin);
  }*/

  if(0) // первичная установка значений
  {
    this->nTotalPower = 391;
    this->generator_total_power_eeprom.save(&this->nTotalPower);
  }

  this->set_output_value(GC_VAL_MOTOHR, (float)(this->tMotoHr/60));
  this->set_output_value(GC_VAL_GAS, (float)(this->tOilMin/60));
  this->set_output_value(GC_VAL_TOTALPOWER_SAVE, (float)(this->nTotalPower));

  this->tEnginOnBegTime = 0;
  this->bEnginOn = false;
  this->tMotHrSave = iTime()+900; // запись в eeprom раз в 15 минут
  this->set_output_value(GC_VAL_GAS_SET, 0);

  if( this->last_control_ac_ )  { this->start_sequence_ac_ok(); }   // Флаг установлен - запускаем последовательность нормального напряжения
  else                          { this->start_sequence_ac_fail(); } // Флаг сброшен - запускаем последовательность включения генератора
}

void GeneratorControl::loop() 
{
    
  bool current_control_state = this->control_switch_->state;
  bool current_control_ac = this->control_ac_->state;
  int  nTotalPowerVal = (int)this->get_output_value(GC_VAL_TOTALPOWER);

  this->CheckChangeFuelValue();
  this->CheckMotoHrAndOil();

  if( this->tsync_ha_flags<millis() )
  {
    if( nTotalPowerVal!=this->nTotalPower && nTotalPowerVal!=0 )
    {
      this->nTotalPower = nTotalPowerVal;
      this->generator_total_power_eeprom.save(&this->nTotalPower);
      this->set_output_value(GC_VAL_TOTALPOWER_SAVE, (float)(this->nTotalPower));
    }

    if( this->is_binary_valid(GC_IN_AC_CTRL) )
    {
      if( this->get_binary_value(GC_IN_AC_CTRL) ) this->control_ac_->turn_on();
      else                                        this->control_ac_->turn_off();
    }
    this->tsync_ha_flags=millis()+60000; // переопрос раз в минуту 
  }
  
  // Если поступила команда на изменение режима работы
  if( current_control_state != this->last_control_state_ ) 
  {
    this->last_control_state_ = current_control_state;

    this->last_step_time_ = 0; // сброс задержки выполнения при нажатии клавиши
    this->twait_ = 0;
    
    if( current_control_state)  { this->start_sequence(); } // Переключатель включен - запускаем генератор
    else                        { this->stop_sequence();  } // Переключатель выключен - останавливаем последовательность
  }
  
  // Если поступила информация о состоянии сети 220v
  if (current_control_ac != this->last_control_ac_) {
    this->last_control_ac_ = current_control_ac;

    if( current_control_ac )  { this->start_sequence_ac_ok(); }   // Флаг установлен - запускаем последовательность нормального напряжения
    else                      { this->start_sequence_ac_fail(); } // Флаг сброшен - запускаем последовательность включения генератора
  }
    
  // Если последовательность запущена, проверяем время для следующего шага
  if( this->sequence_running_ &&  this->last_step_time_ < millis() ) 
  {
    if( this->twait_<millis() ) // задан таймоут перехода к следующему шагу 
    {
      // выполняем функцию-диспетчер последовательностей 
      this->sequence_step(this->current_regime_, this->current_step_);
      this->last_step_time_ = millis()+500; // каждый шаг выполняется через 500 мсек

      // выводим только езменения переменной 
      if( this->val_timeout!=0 ) { this->set_output_value(GC_VAL_TIMEOUT, (float)0 ); this->val_timeout=0; }
    }
    else
    {
      // публикуем текущее значение таймаута при изменении
      if( this->val_timeout!=((int)(this->twait_-millis())/1000) )
      {
        this->val_timeout=((int)(this->twait_-millis())/1000);
        this->set_output_value(GC_VAL_TIMEOUT, (float)(this->val_timeout) );  
      }
    }
  }
}


void GeneratorControl::CheckChangeFuelValue()
{
  float value = this->get_output_value(GC_VAL_GAS_SET);
  
  if( value>0.1 )
  {
    this->tOilMin = value*60;
    if( this->tOilMin>29794 ) this->tOilMin=29794;
    this->generator_gas_eeprom.save(&this->tOilMin); // объем топлива
    this->set_output_value(GC_VAL_GAS, (float)(this->tOilMin/60));
    this->set_output_value(GC_VAL_GAS_SET, 0); 
  }
  else
  if( value<-0.1 )
  {
    this->tOilMin += (-value)*60;
    if( this->tOilMin>29794 ) this->tOilMin=29794;
    this->generator_gas_eeprom.save(&this->tOilMin); // объем топлива
    this->set_output_value(GC_VAL_GAS, (float)(this->tOilMin/60));
    this->set_output_value(GC_VAL_GAS_SET, 0); 
  }
  
}

void GeneratorControl::CheckMotoHrAndOil() 
{
  if( this->get_analog_value(GC_ADC_AI3)>10 ) // генератор заведен
  {
    if( !this->bEnginOn ) 
    {
      this->tEnginOnBegTime = iTime();
      this->bEnginOn = true;
      this->set_output_value(GC_VAL_MOTOHR, (float)(this->tMotoHr/60));
      this->set_output_value(GC_VAL_GAS, (float)(this->tOilMin/60));
    }
    else
    {
      if( this->tEnginOnBegTime +60 < iTime() ) // раз в минуту наращиваем счетчики
      {
        this->tMotoHr += iTime() - this->tEnginOnBegTime;
        this->tOilMin -= iTime() - this->tEnginOnBegTime;
        if( this->tOilMin<0 ) this->tOilMin = 0;
        this->tEnginOnBegTime = iTime();
        this->set_output_value(GC_VAL_MOTOHR, (float)(this->tMotoHr/60)); // передаем в минутах
        this->set_output_value(GC_VAL_GAS, (float)(this->tOilMin/60));
        if( this->tMotHrSave < iTime() )
        {
          this->generator_motohr_eeprom.save(&this->tMotoHr);// моточасы  
          this->generator_gas_eeprom.save(&this->tOilMin); // объем топлива
    
          this->tMotHrSave = iTime()+900; // запись в eeprom раз в 15 минут
        }
      }
    }
  }
  else
  {
    if( this->bEnginOn ) 
    {
      this->tMotoHr += iTime() - this->tEnginOnBegTime;
      this->tOilMin -= iTime() - this->tEnginOnBegTime;
      this->tEnginOnBegTime = 0;
      this->bEnginOn = false;
      this->set_output_value(GC_VAL_MOTOHR, (float)(this->tMotoHr/60));
      this->set_output_value(GC_VAL_GAS, (float)(this->tOilMin/60));
      this->generator_motohr_eeprom.save(&this->tMotoHr);// моточасы  
      this->generator_gas_eeprom.save(&this->tOilMin); // объем топлива
    
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
  this->control_switch_->turn_on();
  this->sequence_running_ = true;
  this->sequence_set(GC_REGIME_START, GC_STEP_START_BEGIN);
  this->last_step_time_ = millis();
  this->restart=0;
  this->sequence_step(this->current_regime_, this->current_step_);

  if( this->is_binary_valid(GC_IN_AC_CTRL) )
  {
    if( this->get_binary_value(GC_IN_AC_CTRL) ) this->control_ac_->turn_on();
    else                                        this->control_ac_->turn_off();
  }
}

void GeneratorControl::stop_sequence() {
  ESP_LOGI(TAG, "Остановка генератора");
  this->control_switch_->turn_off();
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
        this->stop_sequence();
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
        this->start_sequence();
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
        // если питание не подано, то не запускаем последовательность отключения нагрузки 
        if( this->relays_[GC_RELAY_POWER]->state ) this->sequence_setstep( GC_STEP_STOP_POWER_OFF );
        else                                       this->sequence_setstep( GC_STEP_STOP_ENGINE_OFF );
        break;
        
      case GC_STEP_STOP_POWER_OFF: 
        this->relays_[GC_RELAY_POWER]->turn_off();
        sequence_setdelay(20000); // задержка перед выключением генератора
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

bool GeneratorControl::is_binary_valid(size_t index) const {
  if (index < this->binary_sensors_.size() && this->binary_sensors_[index] != nullptr) return true;
  else                                                                                 return false;
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

float GeneratorControl::get_output_value(size_t index) const {
  if (index < this->output_sensors_.size() && this->output_sensors_[index] != nullptr) {
    return this->output_sensors_[index]->state;
  }
  return 0.0f;
}


} // namespace generator_control
} // namespace esphome