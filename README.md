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
- When nothing is printing it says Ready, Print finished (and how long it took), Print cancelled or Printer off, with
  the temperatures while the printer is on.

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once.
2. In Layout, place the **3D print** tile from the library's Plugins group. 3x2 or larger shows the details.
3. In the inspector, choose the printer's **thumbnail camera** (`camera.<printer>_thumbnail`) as the tile's entity.
   On a screen without pictures choose any of the printer's sensors instead.
4. The sensors start filled in for a printer called Q2 (`sensor.q2_*`). For another name, use **Fill from a device**
   and pick the Moonraker printer; the QIDI Box slots are under the **QIDI Box** group.

## Good to know

- The time left counts down on the screen every second between Home Assistant's updates.
- The finish time comes from Moonraker's time left, so it settles after the first few minutes of a print.
- The spool's colour on the screen is the colour the QIDI Box reports for that slot.
