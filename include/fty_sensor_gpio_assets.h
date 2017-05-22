/*  =========================================================================
    fty_sensor_gpio_assets - 42ITy GPIO assets handler

    Copyright (C) 2014 - 2017 Eaton                                        
                                                                           
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

//  Destroy the fty_sensor_gpio_assets
//FTY_SENSOR_GPIO_EXPORT void
//    fty_sensor_gpio_assets_destroy (fty_sensor_gpio_assets_t **self_p);

//  Self test of this class
FTY_SENSOR_GPIO_EXPORT void
    fty_sensor_gpio_assets_test (bool verbose);

// GPx list accessor
//FTY_SENSOR_GPIO_EXPORT zlistx_t *
//    get_gpx_list(fty_sensor_gpio_assets_t *self);

//  @end

#ifdef __cplusplus
}
#endif

#endif
