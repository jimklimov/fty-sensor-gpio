/*  =========================================================================
    fty_sensor_gpio_assets - 42ITy GPIO assets handler

    Copyright (C) 2014 - 2020 Eaton                                        
                                                                           
    This program is free software; you can redistribute it and/or modify   
    it under the terms of the GNU General Public License as published by   
    the Free Software Foundation; either version 2 of the License, or      
    (at your option) any later version.                                    
                                                                           
    This program is distributed in the hope that it will be useful,        
    but WITHOUT ANY WARRANTY; without even the implied warranty of         
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          
    GNU General Public License for more details.                           
                                                                           
    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.            
    =========================================================================
*/

#ifndef FTY_SENSOR_GPIO_ASSETS_H_INCLUDED
#define FTY_SENSOR_GPIO_ASSETS_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

//  @interface
//  fty_sensor_gpio_assets actor
FTY_SENSOR_GPIO_EXPORT void
    fty_sensor_gpio_assets (zsock_t *pipe, void *args);

//  Create a new fty_sensor_gpio_assets
FTY_SENSOR_GPIO_EXPORT fty_sensor_gpio_assets_t *
    fty_sensor_gpio_assets_new (const char* name);

//  Destroy the fty_sensor_gpio_assets
FTY_SENSOR_GPIO_EXPORT void
    fty_sensor_gpio_assets_destroy (fty_sensor_gpio_assets_t **self_p);

//  Self test of this class
FTY_SENSOR_GPIO_EXPORT void
    fty_sensor_gpio_assets_test (bool verbose);

//  List accessor
FTY_SENSOR_GPIO_EXPORT int
    add_sensor(fty_sensor_gpio_assets_t *self, const char* operation,
    const char* manufacturer, const char* assetname, const char* extname,
    const char* asset_subtype, const char* sensor_type,
    const char* sensor_normal_state, const char* sensor_gpx_number,
    const char* sensor_gpx_direction, const char* sensor_parent,
    const char* sensor_location, const char* sensor_power_source,
    const char* sensor_alarm_message, const char* sensor_alarm_severity);

FTY_SENSOR_GPIO_EXPORT void
    request_sensor_power_source(fty_sensor_gpio_assets_t *self, const char* asset_name);

//  @end

#ifdef __cplusplus
}
#endif

#endif
