#pragma once
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::qidi_print {

// The plugin: a tile ("print") with how a print is coming along, and a card ("detail") with all of it at the size of
// the glass, the thumbnail large and the QIDI Box's four slots under it.
class QidiPrint : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
};

}  // namespace esphome::qidi_print
