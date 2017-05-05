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

// Array of monitored GPI (10 GPI on IPC3000)
struct _gpi_info_t gpi_list[10];

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

/*
static int
s_update_event (zloop_t *loop, int timer_id, void *output)
{
    zstr_send (output, "UPDATE");
    return 0;
}
*/

int main (int argc, char *argv [])
{
    char *config_file = NULL;
    zconfig_t *config = NULL;
//    zconfig_t *config_template = NULL;
    char* actor_name = (char*)FTY_SENSOR_GPIO_AGENT;
    char* endpoint = (char*)"ipc://@/malamute";

    bool verbose = false;
    int argn;
//    int sensor_num;

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
        endpoint = s_get (config, "malamute/endpoint", endpoint);
        actor_name = s_get (config, "malamute/address", actor_name);
    }

    if (verbose)
        zsys_info ("fty_sensor_gpio - started");

    // check env verbose
    if (getenv ("BIOS_LOG_LEVEL") && streq (getenv ("BIOS_LOG_LEVEL"), "LOG_DEBUG"))
        verbose = true;

    zactor_t *server = zactor_new (fty_sensor_gpio_server, (void*) FTY_SENSOR_GPIO_AGENT);

    if (verbose) {
        zstr_sendx (server, "VERBOSE", NULL);
        zsys_info ("fty_sensor_gpio - Agent which manages GPI sensors and GPO devices");
    }

    zstr_sendx (server, "CONNECT", "ipc://@/malamute", FTY_SENSOR_GPIO_AGENT, NULL);
    zstr_sendx (server, "CONSUMER", FTY_PROTO_STREAM_ASSETS, ".*", NULL);
    zstr_sendx (server, "PRODUCER", FTY_PROTO_STREAM_ALERTS_SYS, NULL);
/*
    // Setup an update event message every 2 seconds, to check GPI status
    zloop_t *gpio_status_update = zloop_new();
    zloop_timer (gpio_status_update, 2 * 1000, 0, s_update_event, server);
    zloop_start (gpio_status_update);
    zloop_destroy (&gpio_status_update);
*/
    while (!zsys_interrupted) {
        zmsg_t *msg = zactor_recv (server);
        zmsg_destroy (&msg);
    }

    zactor_destroy (&server);
    return 0;


#if 0
    char* actor_name = FTY_SENSOR_GPIO_AGENT; //(char*)"fty-sensor-gpio";
    char* endpoint = (char*)"ipc://@/malamute";

//    map_string_t map_txt;
    
    for (int i = 0; i < 10; i++) {
        gpi_list[i].name = "";
        gpi_list[i].part_number = "";
        gpi_list[i].type = "unknown";
        gpi_list[i].normal_state = GPIO_STATUS_UNKNOWN;
        gpi_list[i].current_state = GPIO_STATUS_UNKNOWN;
        gpi_list[i].gpi_number = -1;
    }


    //get info from env
/*?? NEEDED ??
    char* srv_name  = getenv("FTY_SENSOR_GPIO_SRV_NAME");
    char* srv_type  = getenv("FTY_SENSOR_GPIO_SRV_TYPE");
    char* srv_stype = getenv("FTY_SENSOR_GPIO_SRV_STYPE");
    char* srv_port  = getenv("FTY_SENSOR_GPIO_SRV_PORT");
*/

    // Parse config file
    if(config_file) {
        sensor_num = 0;
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
        endpoint = s_get (config, "malamute/endpoint", endpoint);
        actor_name = s_get (config, "malamute/address", actor_name);

        // get the sensors list to monitor
        zconfig_t *config_sensors_list = zconfig_locate(config, "sensors");

        zconfig_t *config_sensor_info = zconfig_child (config_sensors_list);
        while (config_sensor_info) {
            // Acquire sensor configuration info
            // And fill our monitoring structure
            char* sensor_name = s_get (config_sensor_info, "name",  NULL);
            if (sensor_name) {
                std::cout << "Sensor: " << sensor_name << std::endl;
                gpi_list[sensor_num].name = sensor_name;
            }
            char* sensor_partnumber = s_get (config_sensor_info, "part-number",  NULL);
            if (sensor_partnumber) {
                std::cout << "\tPart number: " << sensor_partnumber << std::endl;
                gpi_list[sensor_num].part_number = strdup(sensor_partnumber);
            }
            char* sensor_port = s_get (config_sensor_info, "port",  NULL);
            if (sensor_port) {
                std::cout << "\tPort: " << sensor_port << std::endl;
                gpi_list[sensor_num].gpi_number = std::stoi(sensor_port);
            }

            // Acquire sensor template info (type, default-state, alarm-message)
            // FIXME: open datadir/<sensor_partnumber>.tpl
            string template_file = string("./data/") + string(sensor_partnumber) + string(".tpl");
            config_template = zconfig_load (template_file.c_str());
            if (!config_template) {
                zsys_error ("Failed to load template config file %s: %m", template_file.c_str());
                exit (EXIT_FAILURE);
            }
            char* sensor_type = s_get (config_template, "type",  NULL);
            if (sensor_type) {
                std::cout << "\ttype: " << sensor_type << std::endl;
                gpi_list[sensor_num].type = sensor_type;
            }
            else
                 std::cout << "FAILED to read sensor type" << std::endl;
            char* sensor_normal_state = s_get (config_template, "normal-state",  NULL);
            if (sensor_normal_state) {
                std::cout << "\tsensor_normal_state: " << sensor_normal_state << std::endl;
                if (sensor_normal_state == string("opened"))
                    gpi_list[sensor_num].normal_state = GPIO_STATUS_OPENED;
                else if (sensor_normal_state == string("closed"))
                    gpi_list[sensor_num].normal_state = GPIO_STATUS_CLOSED;
                // else exception...
            }
            // Get info of the next sensor
            config_sensor_info = zconfig_next (config_sensor_info);
            sensor_num++;
        }
    }

//sensors
//    1
//        name          = 1
//        part-number   = DCS001
//        port          = 1
//    2
//        ...

    libgpio_t *self = libgpio_new ();
    assert (self);

// subscribe to ASSETS  and get the list of device.type=sensor
// name='epdu-62'

    mlm_client_t *client = mlm_client_new ();
    if (client == NULL) {
        zsys_error ("mlm_client_new () failed.");
        return -1;
    }

    int rv = mlm_client_connect (client, endpoint, 1000, actor_name);
    if (rv == -1) {
        mlm_client_destroy (&client);
        zsys_error (
                "mlm_client_connect (endpoint = '%s', timeout = '%d', address = '%s') failed.",
                endpoint, 1000, actor_name);
        return -1;
    } 

    rv = mlm_client_set_consumer (client, FTY_PROTO_STREAM_ASSETS, ".*");
    if (rv == -1) {
        mlm_client_destroy (&client);
        zsys_error (
                "mlm_client_set_consumer (stream = '%s', pattern = '%s') failed.",
                FTY_PROTO_STREAM_ASSETS, ".*");
        return -1;
    }


    // Loop on all sensors
    while (!zsys_interrupted) {
        // FIXME: check for new assets of type=sensor and get ext. attribute
        // for 'model'
        // Then check if we have this 'model' in the catalog (data/<model>.tpl)
        // If so, get the port (GPI pin) and add to gpi_list[]

        zmsg_t *message = mlm_client_recv (client);
        //zmsg_t *message = fty_proto_recv_nowait (client);
//        char *message = zstr_recv (client);
        if (message) {            
            fty_proto_t *protocol_message = fty_proto_decode (&message);
            if (protocol_message == NULL) {
                zsys_error ("fty_proto_decode () failed. Received message could not be parsed.");
                continue; //return -1;
            }
            // Since we are subscribed to FTY_PROTO_STREAM_ASSETS,
            // received message should be FTY_PROTO_ASSET message 
            if (fty_proto_id (protocol_message) != FTY_PROTO_ASSET) {
                zsys_error (
                        "Received message is not expected FTY_PROTO_ASSET id, but: '%d'.",
                        fty_proto_id (protocol_message));
                fty_proto_destroy (&protocol_message);
                continue; //return -1;
            }

//            puts (message);
            zsys_debug ("got an FTY_PROTO_ASSET message %s (type: %s, op: %s)",
                fty_proto_name (protocol_message),
                fty_proto_type (protocol_message),
                fty_proto_operation (protocol_message));
            zsys_debug ("Device type: %s", fty_proto_ext_string (protocol_message, "device.type", NULL));
            // model
            free (message);
        }
        else {
            puts ("no message");
            //break;
        }


        for (sensor_num = 0; sensor_num < GPI_MAX_NUM; sensor_num++) {
            if (gpi_list[sensor_num].name == "")
                continue;

            // Get the current sensor status
            gpi_list[sensor_num].current_state = libgpio_read(&self, gpi_list[sensor_num].gpi_number);
            if (gpi_list[sensor_num].current_state == GPIO_STATUS_UNKNOWN) {
                cout << "Can't read GPI sensor #" << gpi_list[sensor_num].gpi_number << " status"  << std::endl;
                continue;
            }
            cout << "Read " << libgpio_get_status_string(&self, gpi_list[sensor_num].current_state);
            cout << " (value: "  << gpi_list[sensor_num].current_state << ") on GPI #" << gpi_list[sensor_num].gpi_number << std::endl;

            // Check against normal status
            if (gpi_list[sensor_num].current_state != gpi_list[sensor_num].normal_state)
                cout << "ALARM: state changed" << std::endl;
        }
        sleep(2);
    }

    libgpio_destroy (&self);


    return 0;
#endif // #if 0
}
