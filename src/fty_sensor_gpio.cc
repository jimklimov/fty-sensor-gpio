/*  =========================================================================
    fty_sensor_gpio - Manage GPI sensors and GPO devices

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

/*
@header
    fty_sensor_gpio - Manage GPI sensors and GPO devices
@discuss
@end
*/

#include "fty_sensor_gpio_classes.h"

// TODO:
// 2) fty-sensor-gpio starts
// 2.1) fty-sensor-gpio read its configuration file (/etc/fty-sensor-gpio/fty-sensor-gpio.conf)
// 2.2) fty-sensor-gpio loads the needed templates
// 2.3) fty-sensor-gpio main-loop
// =>
// 2) fty-sensor-gpio starts
// 2.1) fty-sensor-gpio read its configuration file (/etc/fty-sensor-gpio/fty-sensor-gpio.conf)
// 2.2) fty-sensor-gpio listen to assets listing, filtering on
//      type=sensor and ext. attribute 'model' known in the supported catalog (data/<model>.tpl)
//      [and parent == self IPC?!]
// 2.3) fty-sensor-gpio loads the needed templates and add the sensor to the monitored list
// 2.4) fty-sensor-gpio main-loop
//     listen to asset additions as for 2.2 / 2.3
//     read status of all configured GPI
//     check the current status of the GPI
//     generate an alarm on the MQ if current state != normal state, using the alarm message or code from the template
// 
// 
// ??? GPO handling ??? postponed
// can be a message on the bus requesting GPOx to be activated

void
usage(){
    puts ("fty-sensor-gpio [options] ...");
    puts ("  -v|--verbose        verbose test output");
    puts ("  -h|--help           this information");
    puts ("  -c|--config         path to config file\n");
    puts ("  -e|--endpoint       malamute endpoint [ipc://@/malamute]");

}

// Send an update request over the MQ to check for GPIO status
static int
s_update_event (zloop_t *loop, int timer_id, void *output)
{
    zstr_send (output, "UPDATE");
    return 0;
}

int main (int argc, char *argv [])
{
    char *config_file = NULL;
    zconfig_t *config = NULL;
    const char* actor_name = (char*)FTY_SENSOR_GPIO_AGENT;
    const char* endpoint = (char*)"ipc://@/malamute";
    const char* str_poll_interval = NULL;
    int poll_interval = DEFAULT_POLL_INTERVAL;
    bool verbose = false;
    int argn;

    // Parse command line
    for (argn = 1; argn < argc; argn++) {
        char *param = NULL;
        if (argn < argc - 1) param = argv [argn+1];

        if (streq (argv [argn], "--help")
        ||  streq (argv [argn], "-h")) {
            usage();
            return 0;
        }
        else if (streq (argv [argn], "--verbose") || streq (argv [argn], "-v")) {
            verbose = true;
        }
        else if (streq (argv [argn], "--config") || streq (argv [argn], "-c")) {
            if (param) config_file = param;
            ++argn;
        }
        else {
            printf ("Unknown option: %s\n", argv [argn]);
            return 1;
        }
    }

    // Parse config file
    if(config_file) {
        zsys_debug ("fty_sensor_gpio:LOAD: %s", config_file);
        config = zconfig_load (config_file);
        if (!config) {
            zsys_error ("Failed to load config file %s: %m", config_file);
            exit (EXIT_FAILURE);
        }
        // VERBOSE
        if (streq (zconfig_get (config, "server/verbose", "false"), "true")) {
            verbose = true;
        }
        // Polling interval
        str_poll_interval = s_get (config, "server/poll_interval", "-1");
        if (str_poll_interval) {
            poll_interval = atoi(str_poll_interval);
        }
        zsys_debug ("Polling interval set to %i", poll_interval);
        endpoint = s_get (config, "malamute/endpoint", endpoint);
        actor_name = s_get (config, "malamute/address", actor_name);
    }

    // check env verbose
    if (getenv ("BIOS_LOG_LEVEL") && streq (getenv ("BIOS_LOG_LEVEL"), "LOG_DEBUG"))
        verbose = true;

    zactor_t *server = zactor_new (fty_sensor_gpio_server, (void*)actor_name);

    if (verbose) {
        zstr_sendx (server, "VERBOSE", NULL);
        zsys_info ("%s - Agent which manages GPI sensors and GPO devices", actor_name);
    }

    zstr_sendx (server, "CONNECT", "ipc://@/malamute", FTY_SENSOR_GPIO_AGENT, NULL);
    zstr_sendx (server, "CONSUMER", FTY_PROTO_STREAM_ASSETS, ".*", NULL);
    zstr_sendx (server, "PRODUCER", FTY_PROTO_STREAM_METRICS, NULL);
    zstr_sendx (server, "PRODUCER", FTY_PROTO_STREAM_ALERTS_SYS, NULL);

    // Setup an update event message every 2 seconds, to check GPI status
    zloop_t *gpio_status_update = zloop_new();
    zloop_timer (gpio_status_update, poll_interval, 0, s_update_event, server);
    zloop_start (gpio_status_update);

    zloop_destroy (&gpio_status_update);
    zactor_destroy (&server);
    return 0;
}
