# QIDI print

How a print on your QIDI printer is coming along, on your Tessera screen. Works with any Klipper printer that Home
Assistant's Moonraker integration knows, and reads the filament from the QIDI Box integration.

## What it shows

- **The tile**: the file being printed, its thumbnail, a progress bar, the percent, the time left and the time it will
  finish, the layer (12 of 140), how long it has been printing, the filament in use (its colour, type and metres used),
  and the nozzle, bed and chamber temperatures. A small tile shows the file, the percent, the time left and the bar. The
  tile turns yellow while the print is paused and red when it failed.
- **The card**: tap the tile. All of it at the size of the screen, the thumbnail large, and the QIDI Box slots at the
  bottom, with the slot in use outlined. Tap the card to close it.
- **The keys**: Pause (Resume while paused) and Stop while a print runs, and Camera at any time (on a printer with a camera). Stop turns red and
  asks for a second tap within 5 seconds before it cancels the print. Camera opens the printer's camera over the whole
  screen; tap it to close. The keys show on the card, and on the tile when it is tall enough (a whole page does).
- When nothing is printing it says Ready, Print finished (and how long it took), Print cancelled or Printer off, with
  the temperatures while the printer is on.

## What it needs

- A QIDI printer (or any Klipper printer) in Home Assistant's [Moonraker integration](https://github.com/marcolivierarsenault/moonraker-home-assistant):
  the Q1 Pro, Plus4, Q2, X-Max 3, X-Plus 3 and the other models that run Klipper.
- For the filament colours and the Box slots: the **Box Slot N Filament** sensors. Support for them in the Moonraker
  integration itself is proposed; until then the [QIDI Box integration](https://github.com/Luca-Bachini-Barr/ha-qidi-box)
  adds them. Without a Box, or without those sensors, the plugin works the same and the filament line shows the
  metres used alone. The tile shows the first Box (four slots).
- A camera is optional. Without one there is no Camera key; the print's thumbnail still shows, because Moonraker makes
  a thumbnail camera for every printer.

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once.
2. In Layout, place the **3D print** tile from the library's Plugins group. 3x2 or larger shows the details and keys.
3. In the inspector, choose the printer's **thumbnail camera** (`camera.<printer>_thumbnail`) as the tile's entity.
   On a screen without pictures choose any of the printer's sensors instead.
4. Press **Fill from a device** and pick the printer. It fills the sensors, the Box slots and the keys'
   buttons and camera, the ones the printer has. Check the camera under **Keys**: Moonraker names it after the webcam,
   so one with an unusual name is chosen by hand.
5. The keys need the screen to be allowed to perform Home Assistant actions (ESPHome, the screen's **Configure**).

## Good to know

- The layer shows only when the print file says how many layers it has. QIDI Studio writes that when its Machine
  G-code has `SET_PRINT_STATS_INFO TOTAL_LAYER=[total_layer_count]` (start) and
  `SET_PRINT_STATS_INFO CURRENT_LAYER={layer_num + 1}` (layer change).
- The time left counts down on the screen every second between Home Assistant's updates.
- The finish time comes from Moonraker's time left, so it settles after the first few minutes of a print.
- The spool's colour on the screen is the colour the QIDI Box reports for that slot.
