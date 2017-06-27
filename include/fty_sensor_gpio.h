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

// TODO: get from config
#define TIMEOUT_MS -1   //wait infinitely

//  Structure to store information on a monitored GPI
//  This includes both the template and configuration information

// Structure of unitary monitored GPx
typedef struct _gpx_info_s {
    char* manufacturer;   // sensor manufacturer name
    char* asset_name;     // sensor asset name
    char* ext_name;       // sensor name
    char* part_number;    // GPI sensor part number
    char* type;           // GPI sensor type (door-contact, ...)
    char* location;       // Parent name, to which the GPIO is attached
    int normal_state;     // opened | closed
    int current_state;    // opened | closed
    int gpx_number;       // GPIO number
    int pin_number;       // Pin number for this GPIO
    int gpx_direction;    // GPI(n) or GPO(ut)
    char* power_source;   // empty for internal, GPO number for externally powered
    char* alarm_message;  // Alert message to publish
    char* alarm_severity; // Applied severity
} _gpx_info_t;

// Config file accessors
const char* s_get (zconfig_t *config, const char* key, std::string &dfl);
const char* s_get (zconfig_t *config, const char* key, const char*dfl);

// Implemented in assets actor
extern zlistx_t *_gpx_list;
extern zlistx_t * get_gpx_list(bool verbose);
extern pthread_mutex_t gpx_list_mutex;

#define my_zsys_debug(verbose, ...) { if (verbose) zsys_debug (__VA_ARGS__); }

#endif
