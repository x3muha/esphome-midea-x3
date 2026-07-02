#ifdef USE_ARDUINO

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "air_conditioner.h"
#include "ac_adapter.h"
#include <cmath>
#include <cstdint>

namespace esphome::midea::ac {

static void set_sensor(Sensor *sensor, float value) {
  if (sensor != nullptr && (!sensor->has_state() || sensor->get_raw_state() != value))
    sensor->publish_state(value);
}

static void set_binary_sensor(BinarySensor *sensor, bool value) {
  if (sensor != nullptr && (!sensor->has_state() || sensor->state != value))
    sensor->publish_state(value);
}

static void set_text_sensor(TextSensor *sensor, const char *value) {
  if (sensor != nullptr && (!sensor->has_state() || sensor->state != value))
    sensor->publish_state(value);
}

template<typename T> void update_property(T &property, const T &value, bool &flag) {
  if (property != value) {
    property = value;
    flag = true;
  }
}

void AirConditioner::loop() {
  ApplianceBase<dudanov::midea::ac::AirConditioner>::loop();
  this->handle_clean_timer_();
}

void AirConditioner::on_status_change() {
  // Add frost protection custom preset once when autoconf completes
  if (this->base_.getAutoconfStatus() == dudanov::midea::AUTOCONF_OK &&
      this->base_.getCapabilities().supportFrostProtectionPreset() && !this->frost_protection_set_) {
    // Read existing presets (set by codegen), append frost protection, write back
    auto traits = this->get_traits();
    const auto &existing = traits.get_supported_custom_presets();
    bool found = false;
    for (const char *p : existing) {
      if (strcmp(p, Constants::FREEZE_PROTECTION) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      std::vector<const char *> merged(existing.begin(), existing.end());
      merged.push_back(Constants::FREEZE_PROTECTION);
      this->set_supported_custom_presets(merged);
    }
    this->frost_protection_set_ = true;
  }
  bool need_publish = false;
  update_property(this->target_temperature, this->base_.getTargetTemp(), need_publish);
  update_property(this->current_temperature, this->base_.getIndoorTemp(), need_publish);
  auto mode = Converters::to_climate_mode(this->base_.getMode());
  update_property(this->mode, mode, need_publish);
  auto swing_mode = Converters::to_climate_swing_mode(this->base_.getSwingMode());
  update_property(this->swing_mode, swing_mode, need_publish);
  // Preset
  auto preset = this->base_.getPreset();
  if (Converters::is_custom_midea_preset(preset)) {
    if (this->set_custom_preset_(Converters::to_custom_climate_preset(preset)))
      need_publish = true;
  } else if (this->set_preset_(Converters::to_climate_preset(preset))) {
    need_publish = true;
  }
  // Fan mode
  auto fan_mode = this->base_.getFanMode();
  if (Converters::is_custom_midea_fan_mode(fan_mode)) {
    if (this->set_custom_fan_mode_(Converters::to_custom_climate_fan_mode(fan_mode)))
      need_publish = true;
  } else if (this->set_fan_mode_(Converters::to_climate_fan_mode(fan_mode))) {
    need_publish = true;
  }
  if (need_publish)
    this->publish_state();
  set_sensor(this->outdoor_sensor_, this->base_.getOutdoorTemp());
  set_sensor(this->power_sensor_, this->base_.getPowerUsage());
  set_sensor(this->humidity_sensor_, this->base_.getIndoorHum());
  this->publish_clean_remaining_();
}

void AirConditioner::control(const ClimateCall &call) {
  dudanov::midea::ac::Control ctrl{};
  auto target_temp_val = call.get_target_temperature();
  if (target_temp_val.has_value())
    ctrl.targetTemp = *target_temp_val;
  auto swing_mode_val = call.get_swing_mode();
  if (swing_mode_val.has_value())
    ctrl.swingMode = Converters::to_midea_swing_mode(*swing_mode_val);
  auto mode_val = call.get_mode();
  if (mode_val.has_value())
    ctrl.mode = Converters::to_midea_mode(*mode_val);
  auto preset_val = call.get_preset();
  if (preset_val.has_value()) {
    ctrl.preset = Converters::to_midea_preset(*preset_val);
  } else if (call.has_custom_preset()) {
    // get_custom_preset() returns StringRef pointing to null-terminated string literals from codegen
    ctrl.preset = Converters::to_midea_preset(call.get_custom_preset().c_str());
  }
  auto fan_mode_val = call.get_fan_mode();
  if (fan_mode_val.has_value()) {
    ctrl.fanMode = Converters::to_midea_fan_mode(*fan_mode_val);
  } else if (call.has_custom_fan_mode()) {
    // get_custom_fan_mode() returns StringRef pointing to null-terminated string literals from codegen
    ctrl.fanMode = Converters::to_midea_fan_mode(call.get_custom_fan_mode().c_str());
  }
  this->base_.control(ctrl);
}

ClimateTraits AirConditioner::traits() {
  auto traits = ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(17);
  traits.set_visual_max_temperature(30);
  traits.set_visual_temperature_step(0.5);
  traits.set_supported_modes(this->supported_modes_);
  traits.set_supported_swing_modes(this->supported_swing_modes_);
  traits.set_supported_presets(this->supported_presets_);
  // Custom fan modes and presets are stored on Climate base class and wired via get_traits()
  /* + MINIMAL SET OF CAPABILITIES */
  traits.add_supported_fan_mode(ClimateFanMode::CLIMATE_FAN_AUTO);
  traits.add_supported_fan_mode(ClimateFanMode::CLIMATE_FAN_LOW);
  traits.add_supported_fan_mode(ClimateFanMode::CLIMATE_FAN_MEDIUM);
  traits.add_supported_fan_mode(ClimateFanMode::CLIMATE_FAN_HIGH);
  if (this->base_.getAutoconfStatus() == dudanov::midea::AUTOCONF_OK) {
    Converters::to_climate_traits(traits, this->base_.getCapabilities());
  }
  if (!traits.get_supported_modes().empty())
    traits.add_supported_mode(ClimateMode::CLIMATE_MODE_OFF);
  if (!traits.get_supported_swing_modes().empty())
    traits.add_supported_swing_mode(ClimateSwingMode::CLIMATE_SWING_OFF);
  if (!traits.get_supported_presets().empty())
    traits.add_supported_preset(ClimatePreset::CLIMATE_PRESET_NONE);
  return traits;
}

void AirConditioner::dump_config() {
  ESP_LOGCONFIG(Constants::TAG,
                "MideaDongle:\n"
                "  [x] Period: %" PRIu32 "ms\n"
                "  [x] Response timeout: %" PRIu32 "ms\n"
                "  [x] Request attempts: %d",
                this->base_.getPeriod(), this->base_.getTimeout(), this->base_.getNumAttempts());
#ifdef USE_REMOTE_TRANSMITTER
  ESP_LOGCONFIG(Constants::TAG, "  [x] Using RemoteTransmitter");
#endif
  if (this->base_.getAutoconfStatus() == dudanov::midea::AUTOCONF_OK) {
    this->base_.getCapabilities().dump();
  } else if (this->base_.getAutoconfStatus() == dudanov::midea::AUTOCONF_ERROR) {
    ESP_LOGW(Constants::TAG,
             "Failed to get 0xB5 capabilities report. Suggest to disable it in config and manually set your "
             "appliance options.");
  }
  this->dump_traits_(Constants::TAG);
}

/* ACTIONS */

void AirConditioner::do_follow_me(float temperature, bool use_fahrenheit, bool beeper) {
#ifdef USE_REMOTE_TRANSMITTER
  // Check if temperature is finite (not NaN or infinite)
  if (!std::isfinite(temperature)) {
    ESP_LOGW(Constants::TAG, "Follow me action requires a finite temperature, got: %f", temperature);
    return;
  }

  // Round and convert temperature to long, then clamp and convert it to uint8_t
  uint8_t temp_uint8 =
      static_cast<uint8_t>(esphome::clamp<long>(std::lroundf(temperature), 0L, static_cast<long>(UINT8_MAX)));

  char temp_symbol = use_fahrenheit ? 'F' : 'C';
  ESP_LOGD(Constants::TAG, "Follow me action called with temperature: %.5f °%c, rounded to: %u °%c", temperature,
           temp_symbol, temp_uint8, temp_symbol);

  // Create and transmit the data
  IrFollowMeData data(temp_uint8, use_fahrenheit, beeper);
  this->transmitter_.transmit(data);
#else
  ESP_LOGW(Constants::TAG, "Action needs remote_transmitter component");
#endif
}

void AirConditioner::do_swing_step() {
#ifdef USE_REMOTE_TRANSMITTER
  IrSpecialData data(0x01);
  this->transmitter_.transmit(data);
#else
  ESP_LOGW(Constants::TAG, "Action needs remote_transmitter component");
#endif
}

void AirConditioner::do_display_toggle() {
  if (this->base_.getCapabilities().supportLightControl()) {
    this->base_.displayToggle();
  } else {
#ifdef USE_REMOTE_TRANSMITTER
    IrSpecialData data(0x08);
    this->transmitter_.transmit(data);
#else
    ESP_LOGW(Constants::TAG, "Action needs remote_transmitter component");
#endif
  }
}

void AirConditioner::do_fresh_on() {
  this->transmit_fresh_(true);
  this->set_fresh_state_(true, "api_fresh");
}

void AirConditioner::do_fresh_off() {
  this->transmit_fresh_(false);
  this->set_fresh_state_(false, "api_fresh");
}

void AirConditioner::do_clean_on() {
  this->save_clean_restore_state_();
  this->transmit_clean_(true);
  this->clean_started_ms_ = millis();
  this->set_clean_running_(true);
  this->set_clean_state_("running");
  this->set_last_control_source_("api_clean");
  this->publish_clean_remaining_();
}

void AirConditioner::do_clean_off() {
  this->transmit_clean_(false);
  this->set_clean_running_(false);
  this->set_clean_state_("idle");
  this->set_last_control_source_("api_clean");
  if (this->clean_restore_)
    this->restore_clean_state_();
}

void AirConditioner::do_clean_reset() {
  this->set_clean_running_(false);
  this->clean_restore_valid_ = false;
  this->set_clean_state_("idle");
  this->set_last_control_source_("api_clean_reset");
  this->publish_clean_remaining_();
}

void AirConditioner::set_fresh_state_(bool state, const char *source) {
  this->fresh_state_ = state;
  set_binary_sensor(this->fresh_state_binary_sensor_, state);
  this->set_last_control_source_(source);
}

void AirConditioner::set_clean_state_(const char *state) { set_text_sensor(this->clean_state_text_sensor_, state); }

void AirConditioner::set_last_control_source_(const char *source) {
  set_text_sensor(this->last_control_source_text_sensor_, source);
}

void AirConditioner::set_clean_running_(bool running) {
  this->clean_running_ = running;
  set_binary_sensor(this->clean_running_binary_sensor_, running);
}

void AirConditioner::publish_clean_remaining_() {
  if (this->clean_remaining_sensor_ == nullptr)
    return;
  if (!this->clean_running_) {
    set_sensor(this->clean_remaining_sensor_, 0);
    return;
  }
  uint32_t elapsed = millis() - this->clean_started_ms_;
  uint32_t remaining = elapsed >= this->clean_duration_ms_ ? 0 : this->clean_duration_ms_ - elapsed;
  set_sensor(this->clean_remaining_sensor_, (remaining + 59999) / 60000);
}

void AirConditioner::save_clean_restore_state_() {
  this->clean_restore_valid_ = true;
  this->clean_restore_fresh_ = this->fresh_state_;
  this->clean_restore_mode_ = this->mode;
  this->clean_restore_target_temperature_ = this->target_temperature;
  this->clean_restore_fan_mode_ = this->fan_mode;
}

void AirConditioner::restore_clean_state_() {
  if (!this->clean_restore_valid_)
    return;
  auto call = this->make_call();
  call.set_mode(this->clean_restore_mode_);
  if (std::isfinite(this->clean_restore_target_temperature_))
    call.set_target_temperature(this->clean_restore_target_temperature_);
  if (this->clean_restore_fan_mode_.has_value())
    call.set_fan_mode(this->clean_restore_fan_mode_);
  call.perform();
  if (this->clean_restore_fresh_)
    this->transmit_fresh_(true);
  this->set_fresh_state_(this->clean_restore_fresh_, "clean_restore");
  this->set_last_control_source_("clean_restore");
  this->clean_restore_valid_ = false;
}

void AirConditioner::handle_clean_timer_() {
  if (!this->clean_running_)
    return;
  this->publish_clean_remaining_();
  if (millis() - this->clean_started_ms_ < this->clean_duration_ms_)
    return;
  this->set_clean_running_(false);
  if (this->clean_restore_) {
    this->set_clean_state_("restoring");
    this->restore_clean_state_();
    this->set_clean_state_("restored");
  } else {
    this->set_clean_state_("probably_finished");
  }
}

void AirConditioner::transmit_fresh_(bool state) {
#ifdef USE_REMOTE_TRANSMITTER
  if (state) {
    IrData data({0xB9, 0x46, 0xF5, 0x0A, 0x41, 0xBE});
    this->transmitter_.transmit_demodulated(data);
  } else {
    IrData data({0xB9, 0x46, 0xF5, 0x0A, 0x42, 0xBD});
    this->transmitter_.transmit_demodulated(data);
  }
#else
  ESP_LOGW(Constants::TAG, "Action needs remote_transmitter component");
#endif
}

void AirConditioner::transmit_clean_(bool state) {
#ifdef USE_REMOTE_TRANSMITTER
  if (state) {
    IrData data({0xB9, 0x46, 0xF5, 0x0A, 0x4F, 0xB0});
    this->transmitter_.transmit_demodulated(data);
  } else {
    IrData data({0xB9, 0x46, 0xF5, 0x0A, 0x50, 0xAF});
    this->transmitter_.transmit_demodulated(data);
  }
#else
  ESP_LOGW(Constants::TAG, "Action needs remote_transmitter component");
#endif
}

}  // namespace esphome::midea::ac

#endif  // USE_ARDUINO
