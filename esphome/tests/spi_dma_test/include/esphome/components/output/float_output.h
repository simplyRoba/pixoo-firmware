#pragma once
namespace esphome::output { class FloatOutput { public: virtual ~FloatOutput()=default; virtual void set_level(float)=0; }; }
