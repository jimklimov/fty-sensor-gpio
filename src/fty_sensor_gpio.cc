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

//zlistx_t *_gpx_list = NULL;

// TODO:
// * REQ fty-asset to REPUBLISH /$all (better than just listening for repub and persist!)
// * Add 'location' / parent.name
//   + Asset management and update existing entries
// * Dedicated alert actor
// * Tests
// * Documentation
// * GPO handling (MAILBOX message requesting GPOx to be activated)
// * Sensors manifest (MAILBOX message requesting the list of supported sensors
//   and details, inc. normal state)

void
usage(){
    puts ("fty-sensor-gpio [options] ...");
    puts ("  -v|--verbose        verbose test output");
    puts ("  -h|--help           this information");
    puts ("  -c|--config         path to config file\n");
    puts ("  -e|--endpoint       malamute endpoint [ipc://@/malamute]");

}

//  --------------------------------------------------------------------------
//  Return a copy of the list of monitored sensors
#if 0
zlistx_t *
get_gpx_list()
{
    return zlistx_dup (_gpx_list); 
}
#endif

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
        str_poll_interval = s_get (config, "server/poll_interval", "2000");
        if (str_poll_interval) {
            poll_interval = atoi(str_poll_interval);
        }
        zsys_debug ("Polling interval set to %i", poll_interval);
        endpoint = s_get (config, "malamute/endpoint", endpoint);
        actor_name = s_get (config, "malamute/address", actor_name);
    }

    // Check env. variables
    if (getenv ("BIOS_LOG_LEVEL") && streq (getenv ("BIOS_LOG_LEVEL"), "LOG_DEBUG"))
        verbose = true;

    zactor_t *assets = zactor_new (fty_sensor_gpio_assets, (void*)"gpio-assets"); //actor_name);
    zactor_t *server = zactor_new (fty_sensor_gpio_server, (void*)actor_name);
//    zactor_t *alerts = zactor_new (fty_sensor_gpio_alerts, (void*)actor_name);

    if (verbose) {
        zstr_sendx (server, "VERBOSE", NULL);
        zstr_sendx (assets, "VERBOSE", NULL);
        //zstr_sendx (alerts, "VERBOSE", NULL);
        zsys_info ("%s - Agent which manages GPI sensors and GPO devices", actor_name);
    }

    // 1rst stream to handle assets
    //zstr_sendx (assets, "CONNECT", "ipc://@/malamute", FTY_SENSOR_GPIO_AGENT"-assets", NULL);
    zstr_sendx (assets, "CONNECT", endpoint, NULL);
    zstr_sendx (assets, "PRODUCER", FTY_PROTO_STREAM_ASSETS, NULL);
    zstr_sendx (assets, "CONSUMER", FTY_PROTO_STREAM_ASSETS, ".*", NULL);

    // 2nd (main) stream to handle GPx polling, metrics publication and mailbox requests
    zstr_sendx (server, "CONNECT", endpoint, NULL);
//    zstr_sendx (assets, "CONSUMER", FTY_PROTO_STREAM_ASSETS, ".*", NULL);
    zstr_sendx (server, "PRODUCER", FTY_PROTO_STREAM_METRICS_SENSOR, NULL);

    // 3rd stream to publish and manage alerts
//    zstr_sendx (alerts, "CONNECT", endpoint, NULL);
//    zstr_sendx (alerts, "PRODUCER", FTY_PROTO_STREAM_ALERTS_SYS, NULL);

    // Setup an update event message every x seconds, to check GPI status
    zloop_t *gpio_status_update = zloop_new();
    zloop_timer (gpio_status_update, poll_interval, 0, s_update_event, server);
    zloop_start (gpio_status_update);

    zloop_destroy (&gpio_status_update);
    zactor_destroy (&server);
    zactor_destroy (&assets);
//    zactor_destroy (&alerts);
    
    // FIXME:
    // cleanup gpx_list
    // ...
    return 0;
}
