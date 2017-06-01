/*  =========================================================================
    fty_sensor_gpio_server - Actor

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
    fty_sensor_gpio_server - Actor
@discuss
     GPIO PROTOCOL

     ------------------------------------------------------------------------
    ## GPO_INTERACTION

    REQ:
        subject: "GPO_INTERACTION"
        Message is a multipart string message

        /asset/action              - apply action (open | close) on asset

    REP:
        subject: "GPO_INTERACTION"
        Message is a multipart message:

        * OK                         = action applied successfully
        * ERROR/<reason>

        where:
            <reason>          = ASSET_NOT_FOUND / SET_VALUE_FAILED / UNKNOWN_VALUE / BAD_COMMAND

     ------------------------------------------------------------------------
    ## GPIO_MANIFEST

    REQ:
        subject: "GPIO_MANIFEST"
        Message is a multipart string message

        /<sensor 1 part number>/.../<sensor N part number>      - get information on sensor(s)

        where:
            <sensor x part number>   = Part number of the sensor(s), to get information on
                                       when empty, return all supported sensors information

    REP:
        subject: "GPIO_MANIFEST"
        Message is a multipart message:

        * OK/<sensor 1 description>/.../<sensor N description N> = non-empty
        * ERROR/<reason>

        where:
            <reason>                 = ASSET_NOT_FOUND / BAD_COMMAND
            <sensor N description N> = sensor_partnumber/manufacturer/type/normal_state/gpx_direction/alarm_severity/alarm_message

@end
*/

#include "fty_sensor_gpio_classes.h"

//  Structure of our class

struct _fty_sensor_gpio_server_t {
    bool               verbose;       // is actor verbose or not
    char               *name;         // actor name
    mlm_client_t       *mlm;          // malamute client
    libgpio_t          *gpio_lib;     // GPIO library handle
    bool               test_mode;     // true if we are in test mode, false otherwise
};

// Configuration accessors
// FIXME: why do we need that? zconfig_get should already do this, no?
const char*
s_get (zconfig_t *config, const char* key, std::string &dfl) {
    assert (config);
    char *ret = zconfig_get (config, key, dfl.c_str());
    if (!ret || streq (ret, ""))
        return (char*)dfl.c_str();
    return ret;
}

const char*
s_get (zconfig_t *config, const char* key, const char*dfl) {
    assert (config);
    char *ret = zconfig_get (config, key, dfl);
    if (!ret || streq (ret, ""))
        return dfl;
    return ret;
}

//  --------------------------------------------------------------------------
//  Publish status of the pointed GPIO sensor

void publish_status (fty_sensor_gpio_server_t *self, _gpx_info_t *sensor, int ttl)
{
    my_zsys_debug(self->verbose, "Publishing GPIO sensor %i (%s) status",
        sensor->gpx_number, sensor->asset_name);

        zhash_t *aux = zhash_new ();
        zhash_autofree (aux);
        char* port = (char*)malloc(5);
        sprintf(port, "GPI%i", sensor->gpx_number);
        zhash_insert (aux, "port", (void*) port);
        string msg_type = string("status.") + port;

        zmsg_t *msg = fty_proto_encode_metric (
            aux,
            time (NULL),
            ttl,
            msg_type.c_str (),
            sensor->asset_name, //sensor->location,
            libgpio_get_status_string(sensor->current_state).c_str(),
            "");
        zhash_destroy (&aux);
        if (msg) {
            std::string topic = string("status.") + port + string("@") + sensor->asset_name; //sensor->location;

            my_zsys_debug(self->verbose, "\tPort: %s, type: %s, status: %s",
                port, msg_type.c_str(),
                libgpio_get_status_string(sensor->current_state).c_str());

            int r = mlm_client_send (self->mlm, topic.c_str (), &msg);
            if( r != 0 )
                my_zsys_debug(self->verbose, "failed to send measurement %s result %", topic.c_str(), r);
            zmsg_destroy (&msg);
        }
        free(port);
}

//  --------------------------------------------------------------------------
//  Check GPIO status and generate alarms if needed

static void
s_check_gpio_status(fty_sensor_gpio_server_t *self)
{
    my_zsys_debug (self->verbose, "%s_server: %s", self->name, __func__);

    pthread_mutex_lock (&gpx_list_mutex);

    // number of sensors monitored in gpx_list
    zlistx_t *gpx_list = get_gpx_list(self->verbose);
    if (!gpx_list) {
        my_zsys_debug (self->verbose, "GPx list not initialized, skipping");
        pthread_mutex_unlock (&gpx_list_mutex);
        return;
    }
    int sensors_count = zlistx_size (gpx_list);
    _gpx_info_t *gpx_info = NULL;

    if (sensors_count == 0) {
        my_zsys_debug (self->verbose, "No sensors monitored");
        pthread_mutex_unlock (&gpx_list_mutex);
        return;
    }
    else
        my_zsys_debug (self->verbose, "%i sensor(s) monitored", sensors_count);

    if(!mlm_client_connected(self->mlm)) {
        pthread_mutex_unlock (&gpx_list_mutex);
        return;
    }

    // Acquire the current sensor
    gpx_info = (_gpx_info_t *)zlistx_first (gpx_list);

    // Loop on all sensors
    for (int cur_sensor_num = 0; cur_sensor_num < sensors_count; cur_sensor_num++) {

        // FIXME: publish also GPO status?
        // For now, filter these out!
        if (gpx_info->gpx_direction != GPIO_DIRECTION_OUT) {
            // Get the current sensor status
            gpx_info->current_state = libgpio_read( self->gpio_lib,
                                                    gpx_info->gpx_number,
                                                    gpx_info->gpx_direction);
            if (gpx_info->current_state == GPIO_STATE_UNKNOWN) {
                my_zsys_debug (self->verbose, "Can't read GPI sensor #%i status", gpx_info->gpx_number);
            }
            else {
                my_zsys_debug (self->verbose, "Read '%s' (value: %i) on GPx sensor #%i (%s/%s)",
                    libgpio_get_status_string(gpx_info->current_state).c_str(),
                    gpx_info->current_state, gpx_info->gpx_number,
                    gpx_info->ext_name, gpx_info->asset_name);

                publish_status (self, gpx_info, 300);
            }
        }
        else
            my_zsys_debug (self->verbose, "GPO sensor '%s' skipped!", gpx_info->asset_name);

        gpx_info = (_gpx_info_t *)zlistx_next (gpx_list);
    }
    pthread_mutex_unlock (&gpx_list_mutex);
}

//  --------------------------------------------------------------------------
//  process message from MAILBOX DELIVER
void static
s_handle_mailbox(fty_sensor_gpio_server_t* self, zmsg_t *message)
{
    std::string subject = mlm_client_subject (self->mlm);
    std::string command = zmsg_popstr (message);
    if (command == "") {
        zmsg_destroy (&message);
        zsys_warning ("Empty subject.");
        return;
    }

    if (command != "GET") {
        zsys_warning ("%s: Received unexpected command '%s'", self->name, command.c_str());
        zmsg_t *reply = zmsg_new ();
        zmsg_addstr(reply, "ERROR");
        zmsg_addstr (reply, "BAD_COMMAND");
        mlm_client_sendto (self->mlm, mlm_client_sender (self->mlm), subject.c_str(), NULL, 1000, &reply);
        zmsg_destroy (&reply);
        return;
    }

    //we assume all request command are MAILBOX DELIVER, and subject="gpio"
    if ( (subject != "") && (subject != "GPIO") && (subject != "GPO_INTERACTION")
         && (subject != "GPIO_MANIFEST") && (subject != "GPIO_TEST")) {
        zsys_warning ("%s: Received unexpected subject '%s'", self->name, subject.c_str());
        zmsg_t *reply = zmsg_new ();
        zmsg_addstr(reply, "ERROR");
        zmsg_addstr (reply, "BAD_COMMAND");
        mlm_client_sendto (self->mlm, mlm_client_sender (self->mlm), subject.c_str(), NULL, 1000, &reply);
        zmsg_destroy (&reply);
        return;
    }
    else {
        zmsg_t *reply = zmsg_new ();
        my_zsys_debug (self->verbose, "%s: '%s' requested", self->name, subject.c_str());

        if (subject == "GPIO") {
            ; // FIXME: needed?
        }
        else if (subject == "GPO_INTERACTION") {
            char *asset_name = zmsg_popstr (message);
            char *action_name = zmsg_popstr (message);
            my_zsys_debug (self->verbose, "GPO_INTERACTION: do '%s' on '%s'",
                action_name, asset_name);
            // Get the GPO entry for details
            pthread_mutex_lock (&gpx_list_mutex);
            zlistx_t *gpx_list = get_gpx_list(self->verbose);
            if (gpx_list) {
                // FIXME: doesn't work?!
                // _gpx_info_t *gpx_info = (_gpx_info_t*)zlistx_find (test_gpx_list, (void *) gpx_info_test);
                int sensors_count = zlistx_size (gpx_list);
                _gpx_info_t *gpx_info = (_gpx_info_t *)zlistx_first (gpx_list);
                gpx_info = (_gpx_info_t *)zlistx_next (gpx_list);
                for (int cur_sensor_num = 0; cur_sensor_num < sensors_count; cur_sensor_num++) {
                    if (!gpx_info || !gpx_info->asset_name)
                        continue;
                    if (streq(gpx_info->asset_name, asset_name))
                        break;
                    gpx_info = (_gpx_info_t *)zlistx_next (gpx_list);
                }
                if ( (gpx_info) && (streq(gpx_info->asset_name, asset_name)) ) {
                    int status_value = libgpio_get_status_value (action_name);

                    if (status_value != GPIO_STATE_UNKNOWN) {
                        if (libgpio_write (self->gpio_lib, gpx_info->gpx_number, status_value) != 0) {
                            my_zsys_debug (self->verbose, "GPO_INTERACTION: failed to set value!");
                            zmsg_addstr (reply, "ERROR");
                            zmsg_addstr (reply, "SET_VALUE_FAILED");
                        }
                        else {
                            zmsg_addstr (reply, "OK");
                        }
                    }
                    else {
                        my_zsys_debug (self->verbose, "GPO_INTERACTION: status value is unknown!");
                        zmsg_addstr (reply, "ERROR");
                        zmsg_addstr (reply, "UNKNOWN_VALUE");
                    }
                }
                else {
                    my_zsys_debug (self->verbose, "GPO_INTERACTION: can't find asset '%'!", asset_name);
                    zmsg_addstr (reply, "ERROR");
                    zmsg_addstr (reply, "ASSET_NOT_FOUND");
                }
                // send the reply
                int rv = mlm_client_sendto (self->mlm, mlm_client_sender (self->mlm), subject.c_str(), NULL, 5000, &reply);
                if (rv == -1)
                    zsys_error ("%s:\tgpio: mlm_client_sendto failed", self->name);
            }
            pthread_mutex_unlock (&gpx_list_mutex);
            zmsg_destroy (&reply);
        }
        else if (subject == "GPIO_MANIFEST") {
            // FIXME: consolidate code using filters
            zmsg_t *reply = zmsg_new ();
            zmsg_addstr (reply, "OK");
            char *asset_partnumber = zmsg_popstr (message);
            // Check for a parameter, to send (a) specific template(s)
            if (asset_partnumber) {
                while (asset_partnumber) {
                    // FIXME: use @datadir@ (how?)!
                    string template_filename = string("./data/") + string(asset_partnumber) + string(".tpl");

                    // We have a GPIO sensor, process it
                    zconfig_t *sensor_template_file = zconfig_load (template_filename.c_str());
                    if (!sensor_template_file) {
                        my_zsys_debug (self->verbose, "Can't load sensor template file"); // FIXME: error
                        zmsg_addstr (reply, "ERROR");
                        zmsg_addstr (reply, "ASSET_NOT_FOUND");
                        // FIXME: should we break for 1 issue or?
                        break;
                    }
                    else {
                        my_zsys_debug (self->verbose, "Template file found for %s", asset_partnumber);
                        // Get info from template
                        const char *manufacturer = s_get (sensor_template_file, "manufacturer", "");
                        const char *type = s_get (sensor_template_file, "type", "");
                        const char *normal_state = s_get (sensor_template_file, "normal-state", "");
                        const char *gpx_direction = s_get (sensor_template_file, "gpx-direction", "");
                        const char *alarm_severity = s_get (sensor_template_file, "alarm-severity", "");
                        const char *alarm_message = s_get (sensor_template_file, "alarm-message", "");

                        zmsg_addstr (reply, asset_partnumber);
                        zmsg_addstr (reply, manufacturer);
                        zmsg_addstr (reply, type);
                        zmsg_addstr (reply, normal_state);
                        zmsg_addstr (reply, gpx_direction);
                        zmsg_addstr (reply, alarm_severity);
                        zmsg_addstr (reply, alarm_message);
                    }

                    // Get the next one, if there is one
                    asset_partnumber = zmsg_popstr (message);
                }
                // send the reply
                int rv = mlm_client_sendto (self->mlm, mlm_client_sender (self->mlm), subject.c_str(), NULL, 5000, &reply);
                if (rv == -1)
                    zsys_error ("%s:\tgpio: mlm_client_sendto failed", self->name);

                zmsg_destroy (&reply);
            }
            else {
                // Send all templates
/*                zdir_t *older = zdir_new ("./data/", NULL);
                zdir_count (zdir_t *self);
                zlist_t *zdir_list (zdir_t *self);
*/
            }

/*
{
    assert (path_to_dir);

    zdir_t *dir = zdir_new (path_to_dir, "-");
    if (!dir) {
        log_error ("zdir_new (path = '%s', parent = '-') failed.", path_to_dir);
        return 1;
    }

    zlist_t *files = zdir_list (dir);
    if (!files) {
        zdir_destroy (&dir);
        log_error ("zdir_list () failed.");
        return 1;
    }

    std::regex file_rex (".+\\.tpl");

    zfile_t *item = (zfile_t *) zlist_first (files);
    while (item) {
        if (std::regex_match (zfile_filename (item, path_to_dir), file_rex)) {
            std::string filename = zfile_filename (item, path_to_dir);
            filename.erase (filename.size () - 4);
            std::string service = "fty-metric-composite@";
            service += filename;
            s_bits_systemctl ("stop", service.c_str ());
            s_bits_systemctl ("disable", service.c_str ());
            zfile_remove (item);
            log_debug ("file removed");
        }
        item = (zfile_t *) zlist_next (files);
    }
    zlist_destroy (&files);
    zdir_destroy (&dir);
    return 0;
}

DCS001
    manufacturer   = Eaton
    part-number    = DCS001
    type           = door-contact-sensor
    normal-state   = closed
    gpx-direction  = GPI
    alarm-severity = WARNING
    alarm-message  = Door has been $status
*/
        }
        else if (subject == "GPIO_TEST") {
            ;
        }
    }
}

//  --------------------------------------------------------------------------
//  Create a new fty_sensor_gpio_server

fty_sensor_gpio_server_t *
fty_sensor_gpio_server_new (const char* name)
{
    fty_sensor_gpio_server_t *self = (fty_sensor_gpio_server_t *) zmalloc (sizeof (fty_sensor_gpio_server_t));
    assert (self);

    //  Initialize class properties
    self->mlm         = mlm_client_new();
    self->name        = strdup(name);
    self->verbose     = false;
    self->test_mode   = false;

    self->gpio_lib = libgpio_new ();
    assert (self->gpio_lib);

    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the fty_sensor_gpio_server

void
fty_sensor_gpio_server_destroy (fty_sensor_gpio_server_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        fty_sensor_gpio_server_t *self = *self_p;

        //  Free class properties
        libgpio_destroy (&self->gpio_lib);
        free(self->name);
        mlm_client_destroy (&self->mlm);

        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Create a new fty_info_server

void
fty_sensor_gpio_server (zsock_t *pipe, void *args)
{
    char *name = (char *)args;
    if (!name) {
        zsys_error ("Adress for fty-sensor-gpio actor is NULL");
        return;
    }

    fty_sensor_gpio_server_t *self = fty_sensor_gpio_server_new(name);
    assert (self);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (self->mlm), NULL);
    assert (poller);

    zsock_signal (pipe, 0); 
    zsys_info ("%s_server: Started", self->name);

    while (!zsys_interrupted)
    {
        void *which = zpoller_wait (poller, TIMEOUT_MS);
        if (which == NULL) {
            if (zpoller_terminated (poller) || zsys_interrupted) {
                break;
            }
        }
        if (which == pipe) {
            zmsg_t *message = zmsg_recv (pipe);
            char *cmd = zmsg_popstr (message);
            my_zsys_debug(self->verbose, "fty_sensor_gpio: received command %s", cmd);
            if (cmd) {
                if (streq (cmd, "$TERM")) {
                    zstr_free (&cmd);
                    zmsg_destroy (&message);
                    break;
                }
                else if (streq (cmd, "CONNECT")) {
                    char *endpoint = zmsg_popstr (message);
                     if (!endpoint)
                        zsys_error ("%s:\tMissing endpoint", self->name);
                    assert (endpoint);
                    int r = mlm_client_connect (self->mlm, endpoint, 5000, self->name);
                    if (r == -1)
                        zsys_error ("%s:\tConnection to endpoint '%s' failed", self->name, endpoint);
                    zsys_debug("fty-gpio-sensor-server: CONNECT %s/%s", endpoint, self->name);
                    zstr_free (&endpoint);
                }
                else if (streq (cmd, "PRODUCER")) {
                    char *stream = zmsg_popstr (message);
                    assert (stream);
                    mlm_client_set_producer (self->mlm, stream);
                    my_zsys_debug (self->verbose, "fty_sensor_gpio: setting PRODUCER on %s", stream);
                    zstr_free (&stream);
                }
                else if (streq (cmd, "CONSUMER")) {
                    char *stream = zmsg_popstr (message);
                    char *pattern = zmsg_popstr (message);
                    assert (stream && pattern);
                    mlm_client_set_consumer (self->mlm, stream, pattern);
                    my_zsys_debug (self->verbose, "fty_sensor_gpio: setting CONSUMER on %s/%s", stream, pattern);
                    zstr_free (&stream);
                    zstr_free (&pattern);
                }
                else if (streq (cmd, "VERBOSE")) {
                    self->verbose = true;
                    my_zsys_debug (self->verbose, "fty_sensor_gpio: VERBOSE=true");
                    libgpio_set_verbose(self->gpio_lib, self->verbose);
                }
                else if (streq (cmd, "TEST")) {
                    self->test_mode = true;
                    libgpio_set_test_mode (self->gpio_lib, self->test_mode);
                    my_zsys_debug (self->verbose, "fty_sensor_gpio: TEST=true");
                }
                else if (streq (cmd, "UPDATE")) {
                    s_check_gpio_status(self);
                }
                else if (streq (cmd, "GPIO_CHIP_ADDRESS")) {
                    char *str_gpio_base_address = zmsg_popstr (message);
                    int gpio_base_address = atoi(str_gpio_base_address);
                    libgpio_set_gpio_base_address (self->gpio_lib, gpio_base_address);
                    my_zsys_debug (self->verbose, "fty_sensor_gpio: GPIO_CHIP_ADDRESS=%i", gpio_base_address);
                }
                else if (streq (cmd, "GPI_OFFSET")) {
                    char *str_gpi_offset = zmsg_popstr (message);
                    int gpi_offset = atoi(str_gpi_offset);
                    libgpio_set_gpi_offset (self->gpio_lib, gpi_offset);
                    my_zsys_debug (self->verbose, "fty_sensor_gpio: GPI_OFFSET=%i", gpi_offset);
                }
                else if (streq (cmd, "GPO_OFFSET")) {
                    char *str_gpo_offset = zmsg_popstr (message);
                    int gpo_offset = atoi(str_gpo_offset);
                    libgpio_set_gpo_offset (self->gpio_lib, gpo_offset);
                    my_zsys_debug (self->verbose, "fty_sensor_gpio: GPO_OFFSET=%i", gpo_offset);
                }
                else if (streq (cmd, "GPI_COUNT")) {
                    char *str_gpi_count = zmsg_popstr (message);
                    int gpi_count = atoi(str_gpi_count);
                    libgpio_set_gpi_count (self->gpio_lib, gpi_count);
                    my_zsys_debug (self->verbose, "fty_sensor_gpio: GPI_COUNT=%i", gpi_count);
                }
                else if (streq (cmd, "GPO_COUNT")) {
                    char *str_gpo_count = zmsg_popstr (message);
                    int gpo_count = atoi(str_gpo_count);
                    libgpio_set_gpo_count (self->gpio_lib, gpo_count);
                    my_zsys_debug (self->verbose, "fty_sensor_gpio: GPO_COUNT=%i", gpo_count);
                }
                else {
                    zsys_warning ("%s:\tUnknown API command=%s, ignoring", __func__, cmd);
                }
                zstr_free (&cmd);
            }
            zmsg_destroy (&message);
        }
        else if (which == mlm_client_msgpipe (self->mlm)) {
            zmsg_t *message = mlm_client_recv (self->mlm);
            if (streq (mlm_client_command (self->mlm), "MAILBOX DELIVER")) {
                // someone is addressing us directly
                // FIXME: can be requested for:
                // * sensors manifest (pn, type, normal status) by UI
                //   => handled by _assets?!
                // * GPO interaction: REQ
                s_handle_mailbox(self, message);
            }
            zmsg_destroy (&message);
        }
    }
    zpoller_destroy (&poller);
    mlm_client_destroy (&self->mlm);
}

//  --------------------------------------------------------------------------
//  Self test of this class

// FIXME

void
fty_sensor_gpio_server_test (bool verbose)
{
    printf (" * fty_sensor_gpio_server: ");

    // Test #1: Get status for an asset and check its publication
    // Test #2: Get GPIO_MANIFEST request and answer it
    // Test #3: Get GPO_INTERACTION request and answer it


    //  @selftest
    //  Simple create/destroy test

    // Note: If your selftest reads SCMed fixture data, please keep it in
    // src/selftest-ro; if your test creates filesystem objects, please
    // do so under src/selftest-rw. They are defined below along with a
    // usecase for the variables (assert) to make compilers happy.
    const char *SELFTEST_DIR_RO = "src/selftest-ro";
    const char *SELFTEST_DIR_RW = "src/selftest-rw";
    assert (SELFTEST_DIR_RO);
    assert (SELFTEST_DIR_RW);
    // Uncomment these to use C++ strings in C++ selftest code:
    //std::string str_SELFTEST_DIR_RO = std::string(SELFTEST_DIR_RO);
    //std::string str_SELFTEST_DIR_RW = std::string(SELFTEST_DIR_RW);
    //assert ( (str_SELFTEST_DIR_RO != "") );
    //assert ( (str_SELFTEST_DIR_RW != "") );
    // NOTE that for "char*" context you need (str_SELFTEST_DIR_RO + "/myfilename").c_str()

    fty_sensor_gpio_server_t *self = fty_sensor_gpio_server_new (FTY_SENSOR_GPIO_AGENT);
    assert (self);
    fty_sensor_gpio_server_destroy (&self);
    //  @end
    printf ("OK\n");
}
