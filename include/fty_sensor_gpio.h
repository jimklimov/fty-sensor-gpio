/*  =========================================================================
    fty-sensor-gpio - Agent to manage GPI sensors and GPO devices

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

#ifndef FTY_SENSOR_GPIO_H_H_INCLUDED
#define FTY_SENSOR_GPIO_H_H_INCLUDED

#include <iostream>
#include <sstream>
#include <cstddef>
#include <map>

using namespace std;

//  Include the project library file
#include "fty_sensor_gpio_library.h"

//  Add your own public definitions here, if you need them
#define FTY_SENSOR_GPIO_AGENT "fty-sensor-gpio"
#define DEFAULT_POLL_INTERVAL 2000

//  Structure to store information on a monitored GPI
//  This includes both the template and configuration information

// FIXME: replace with some zlist...
struct _gpx_info_t {
    string name;        // sensor asset name
    string part_number; // GPI sensor part number
    string type;        // GPI sensor type (door-contact, ...)
    int normal_state;   // opened | closed
    int current_state;  // opened | closed
    int gpx_number;     // GPI number, 1 - 10 (FIXME: GPO)
    int gpx_direction;      // GPIn or GPOut
};


// Config file accessors
const char* s_get (zconfig_t *config, const char* key, std::string &dfl);
const char* s_get (zconfig_t *config, const char* key, const char*dfl);


#endif
