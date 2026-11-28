/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file mobile_vehicle_gui.h From depot to running train in the mobile layout. */

#ifndef MOBILE_VEHICLE_GUI_H
#define MOBILE_VEHICLE_GUI_H

#include "command_type.h"
#include "vehicle_type.h"
#include "engine_type.h"
#include "cargo_type.h"
#include "tile_type.h"
#include "window_type.h"
#include "station_type.h"
#include "network/network_type.h"

struct BaseStation;
struct Vehicle;

void ShowMobileDepotWindow(TileIndex tile, VehicleType type);
void ShowMobileRefitWindow(const Vehicle *v);
void ShowMobileGroups(VehicleType type);
void ShowMobileAutoreplace(VehicleType type);
void ShowMobileNewTrainWindow(TileIndex tile, VehicleID target);
void ShowMobileTrainWindow(const Vehicle *v, bool add_stops = false, bool show_details = false);
void MobileStopAddingStops();
void ShowMobileTrainList(WindowNumber number, StationID station, VehicleType type);
bool MobileVehicleOnStationSignClick(const BaseStation *st);
void CcMobileBuildTrainPart(Commands cmd, const CommandCost &result, VehicleID new_veh_id, uint, uint16_t, CargoArray, TileIndex tile, EngineID, bool, CargoType, ClientID);

#endif /* MOBILE_VEHICLE_GUI_H */
