# Changelog

## 0.3.0 - 2026-11-27
- For every QIDI printer on Moonraker (Q1 Pro, Plus4, Q2, X-Max 3, X-Plus 3...), with or without a Box and a camera.
  Nothing is filled in for one printer any more: **Fill from a device** fills the sensors, the Box and the keys.
- Every reading comes from the Moonraker integration. The Box slots read its Box Slot N Filament sensors (proposed in
  moonraker-home-assistant#859). Without a Box the Box row is not drawn; a Box switched on or off while the card is
  open lays the card out again.
- The Box's filament changes, from Moonraker's Box Filament Change sensor: a row says each step (Cutting filament,
  Unloading filament, Loading 1D, Purging 1D, Ejecting 1A) with the colour of the spool it loads.
- Box warnings (Moonraker's Box Warnings) and Box drying (Box 1 Drying) get a row of their own while they apply.
- A key whose entity is not there or is unavailable does not show: no Camera key on a printer without a camera, and no
  Pause, Resume or Stop without the printer's buttons.

## 0.2.0 - 2026-11-27
- Keys on the tile (when it is tall enough) and on the card: Pause or Resume, Stop and Camera. They press the
  printer's own Moonraker buttons; Stop asks for a second tap within 5 seconds before it cancels the print.
- The layer shows only when the file says how many layers it has, and the current one lies within them. Without that,
  Moonraker guesses the layer from the nozzle's height, which jumped around (0, 26, 688, 1) while the printer probed,
  wiped and parked.
- A camera card: the printer's camera over the whole screen, opened by the Camera key; a tap closes it.

## 0.1.0 - 2026-11-27
- First version: a tile with the print's file, thumbnail, progress bar, percent, time left and finish time, layer,
  filament in use, temperatures and speed, laid out for every size from 2x1 to the whole page.
- A wide card with all of it at the size of the glass and the QIDI Box's four slots, the active one outlined.
