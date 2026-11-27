# QIDI print

How a print on your QIDI printer is coming along, on your Tessera screen. Works with any Klipper printer that Home
Assistant's Moonraker integration knows, and reads the filament from the QIDI Box integration.

## What it shows

- **The tile**: the file being printed, its thumbnail, a progress bar, the percent, the time left and the time it will
  finish, the layer (12 of 140), how long it has been printing, the filament in use (its colour, type and metres used),
  and the nozzle, bed and chamber temperatures. A small tile shows the file, the percent, the time left and the bar. The
  tile turns yellow while the print is paused and red when it failed.
- **The card**: tap the tile. All of it at the size of the screen, the thumbnail large, and the four QIDI Box slots at
  the bottom with the slot in use outlined. Tap the card to close it.
- **The keys**: Pause (Resume while paused) and Stop while a print runs, and Camera at any time. Stop turns red and
  asks for a second tap within 5 seconds before it cancels the print. Camera opens the printer's camera over the whole
  screen; tap it to close. The keys show on the card, and on the tile when it is tall enough (a whole page does).
- When nothing is printing it says Ready, Print finished (and how long it took), Print cancelled or Printer off, with
  the temperatures while the printer is on.

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once.
2. In Layout, place the **3D print** tile from the library's Plugins group. 3x2 or larger shows the details.
3. In the inspector, choose the printer's **thumbnail camera** (`camera.<printer>_thumbnail`) as the tile's entity.
   On a screen without pictures choose any of the printer's sensors instead.
4. The sensors start filled in for a printer called Q2 (`sensor.q2_*`). For another name, use **Fill from a device**
   and pick the Moonraker printer; the QIDI Box slots are under the **QIDI Box** group, and the buttons and the
   camera under **Keys**.
5. The keys need the screen to be allowed to perform Home Assistant actions (ESPHome, the screen's **Configure**).

## Good to know

- The layer shows only when the print file says how many layers it has. QIDI Studio writes that when its Machine
  G-code has `SET_PRINT_STATS_INFO TOTAL_LAYER=[total_layer_count]` (start) and
  `SET_PRINT_STATS_INFO CURRENT_LAYER={layer_num + 1}` (layer change).
- The time left counts down on the screen every second between Home Assistant's updates.
- The finish time comes from Moonraker's time left, so it settles after the first few minutes of a print.
- The spool's colour on the screen is the colour the QIDI Box reports for that slot.
