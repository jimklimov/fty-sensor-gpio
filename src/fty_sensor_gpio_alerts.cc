/*  =========================================================================
    fty_sensor_gpio_alerts - 42ITy GPIO alerts handler

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
    fty_sensor_gpio_alerts - 42ITy GPIO alerts handler
@discuss
@end
*/

#include "fty_sensor_gpio_classes.h"

//  Structure of our class

struct _fty_sensor_gpio_alerts_t {
    bool               verbose;       // is actor verbose or not
    char               *name;         // actor name
    mlm_client_t       *mlm;          // malamute client
};

//  --------------------------------------------------------------------------
//  Helper function to replace strings

static char*
str_replace(const char *in, const char *pattern, const char *by)
{
    size_t outsize = strlen(in) + 1;
    // TODO maybe avoid reallocing by counting the non-overlapping occurences of pattern
    char *res = (char*)malloc(outsize);
    // use this to iterate over the output
    size_t resoffset = 0;

    char *needle;
    while ((needle = strstr((char *)in, (char *)pattern))) {
        // copy everything up to the pattern
        memcpy(res + resoffset, in, needle - in);
        resoffset += needle - in;

        // skip the pattern in the input-string
        in = needle + strlen(pattern);

        // adjust space for replacement
        outsize = outsize - strlen(pattern) + strlen(by);
        res = (char*)realloc(res, outsize);

        // copy the pattern
        memcpy(res + resoffset, by, strlen(by));
        resoffset += strlen(by);
    }

    // copy the remaining input
    strcpy(res + resoffset, in);

    return res;
}

//  --------------------------------------------------------------------------
//  Publish an alert for the pointed GPIO sensor

void publish_alert (fty_sensor_gpio_alerts_t *self, _gpx_info_t *sensor, int ttl)
{
    my_zsys_debug (self->verbose, "Publishing GPIO sensor %i (%s) alert",
        sensor->gpx_number,
        sensor->asset_name);

    // FIXME: ...
    const char *state = "ACTIVE", *severity = sensor->alarm_severity;
    char* description = (char*)malloc(128);
    sprintf(description, sensor->alarm_message,
        sensor->ext_name);

    // Adapt alarm message if needed
    if (strchr(sensor->alarm_message, '$')) {
        // FIXME: other possible patterns $name $parent_name...
        description = str_replace(sensor->alarm_message,
                                  "$status",
                                  libgpio_get_status_string(sensor->current_state).c_str());
    }


    std::string rule = string(sensor->type) + ".state_change@" + sensor->asset_name;

    my_zsys_debug (self->verbose, "%s: publishing alert %s with description:\n%s", __func__, rule.c_str (), description);
    zmsg_t *message = fty_proto_encode_alert(
        NULL,               // aux
        time (NULL),        // timestamp
        ttl,
        rule.c_str (),      // rule
        sensor->asset_name, // element
        state,              // state
        severity,           // severity
        description,        // description
        ""                  // action ?email
    );
    std::string topic = rule + "/" + severity + "@" + sensor->asset_name;
    if (message) {
        int r = mlm_client_send (self->mlm, topic.c_str (), &message);
        if( r != 0 )
            my_zsys_debug (self->verbose, "failed to send alert %s result %", topic.c_str(), r);
    }
    zmsg_destroy (&message);
}

//  --------------------------------------------------------------------------
//  Check GPIO status and generate alarms if needed

static void
s_check_gpio_status(fty_sensor_gpio_alerts_t *self)
{
    my_zsys_debug (self->verbose, "%s_alerts: %s", self->name, __func__);

    // number of sensors monitored in gpx_list
    zlistx_t *gpx_list = get_gpx_list(self->verbose);
    if (!gpx_list) {
        my_zsys_debug (self->verbose, "GPx list not initialized, skipping");
        return;
    }
    int sensors_count = zlistx_size (gpx_list);
    _gpx_info_t *gpx_info = NULL;

    if (sensors_count == 0) {
        my_zsys_debug (self->verbose, "No sensors monitored");
        return;
    }
    else
        my_zsys_debug (self->verbose, "%i sensor(s) monitored", sensors_count);

    if(!mlm_client_connected(self->mlm))
        return;

    // Acquire the current sensor
    gpx_info = (_gpx_info_t *)zlistx_first (gpx_list);

    // Loop on all sensors
    for (int cur_sensor_num = 0; cur_sensor_num < sensors_count; cur_sensor_num++) {

        // Status has been updated by the main server actor,
        // only check the fields

        // No processing if not yet init'ed!
        if (gpx_info->current_state != GPIO_STATE_UNKNOWN) {
            // Check against normal state
            if (gpx_info->current_state != gpx_info->normal_state) {
                my_zsys_debug (self->verbose, "ALARM: state changed");
                // FIXME: do not repeat alarm?! so maybe flag in self
                publish_alert (self, gpx_info, 300);
            }
        }
        gpx_info = (_gpx_info_t *)zlistx_next (gpx_list);
    }
}

//  --------------------------------------------------------------------------
//  Create a new fty_sensor_gpio_alerts

fty_sensor_gpio_alerts_t *
fty_sensor_gpio_alerts_new (const char* name)
{
    fty_sensor_gpio_alerts_t *self = (fty_sensor_gpio_alerts_t *) zmalloc (sizeof (fty_sensor_gpio_alerts_t));
    assert (self);
    //  Initialize class properties
    self->mlm       = mlm_client_new();
    self->name        = strdup(name);
    self->verbose     = false;

    return self;
}

//  --------------------------------------------------------------------------
//  Create a new fty_info_alerts

void
fty_sensor_gpio_alerts(zsock_t *pipe, void *args)
{
    char *name = (char *)args;
    if (!name) {
        zsys_error ("Adress for fty-sensor-gpio-alerts actor is NULL");
        return;
    }

    fty_sensor_gpio_alerts_t *self = fty_sensor_gpio_alerts_new(name);
    assert (self);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (self->mlm), NULL);
    assert (poller);

    zsock_signal (pipe, 0); 
    zsys_info ("%s_alerts: Started", self->name);

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
            my_zsys_debug (self->verbose, "fty_sensor_gpio: received command %s", cmd);
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
                    my_zsys_debug (self->verbose, "fty-gpio-sensor-alerts: CONNECT %s/%s", endpoint, self->name);
                    zstr_free (&endpoint);
                }
                else if (streq (cmd, "PRODUCER")) {
                    char *stream = zmsg_popstr (message);
                    assert (stream);
                    mlm_client_set_producer (self->mlm, stream);
                    my_zsys_debug (self->verbose, "fty-gpio-sensor-alerts: setting PRODUCER on %s", stream);
                    zstr_free (&stream);
                }
                else if (streq (cmd, "CONSUMER")) {
                    char *stream = zmsg_popstr (message);
                    char *pattern = zmsg_popstr (message);
                    assert (stream && pattern);
                    mlm_client_set_consumer (self->mlm, stream, pattern);
                    my_zsys_debug (self->verbose, "fty-gpio-sensor-alerts: setting CONSUMER on %s/%s", stream, pattern);
                    zstr_free (&stream);
                    zstr_free (&pattern);
                }
                else if (streq (cmd, "VERBOSE")) {
                    self->verbose = true;
                    my_zsys_debug (self->verbose, "fty_sensor_gpio: VERBOSE=true");
                }
                else if (streq (cmd, "UPDATE")) {
                    s_check_gpio_status(self);
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
            if (is_fty_proto (message)) {
                // FIXME: to be removed?
                fty_proto_t *fmessage = fty_proto_decode (&message);
                //if (fty_proto_id (fmessage) == FTY_PROTO_ASSET) {
                //    fty_sensor_gpio_handle_asset (self, fmessage);
                //}
                fty_proto_destroy (&fmessage);
            } else if (streq (mlm_client_command (self->mlm), "MAILBOX DELIVER")) {
                // someone is addressing us directly
            }
            zmsg_destroy (&message);
        }
    }
    zpoller_destroy (&poller);
    mlm_client_destroy (&self->mlm);
}

//  --------------------------------------------------------------------------
//  Destroy the fty_sensor_gpio_alerts

void
fty_sensor_gpio_alerts_destroy (fty_sensor_gpio_alerts_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        fty_sensor_gpio_alerts_t *self = *self_p;
        //  Free class properties
        mlm_client_destroy (&self->mlm);
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
fty_sensor_gpio_alerts_test (bool verbose)
{
    printf (" * fty_sensor_gpio_alerts: ");

    //  @selftest

    static const char* endpoint = "inproc://fty_sensor_gpio_alerts_test";

    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    zstr_sendx (server, "BIND", endpoint, NULL);
    if (verbose)
        zstr_send (server, "VERBOSE");

    zactor_t *alerts = zactor_new (fty_sensor_gpio_alerts, (void*)"gpio-alerts");
    if (verbose)
        zstr_send (alerts, "VERBOSE");
    zstr_sendx (alerts, "CONNECT", endpoint, NULL);
    zstr_sendx (alerts, "PRODUCER", FTY_PROTO_STREAM_ALERTS_SYS, NULL);
    zclock_sleep (1000);

    mlm_client_t *alerts_listener = mlm_client_new ();
    mlm_client_connect (alerts_listener, endpoint, 1000, "fty_sensor_gpio_alerts_listener");
    mlm_client_set_consumer (alerts_listener, FTY_PROTO_STREAM_ALERTS_SYS, ".*");

    // Test #1: Create an asset with current_state != normal_state
    // and check that an alarm is generated
    {
        fty_sensor_gpio_assets_t *assets_self = fty_sensor_gpio_assets_new("gpio-assets");

        int rv = add_sensor(assets_self, "create",
            "Eaton", "sensor-10", "GPIO-Sensor-Door1",
            "sensor", "door-contact-sensor",
            "closed", "1",
            "GPI", "",
            "Door has been $status", "WARNING");

        assert (rv == 0);
        // Acquire the list of monitored sensors
        pthread_mutex_lock (&gpx_list_mutex);
        zlistx_t *test_gpx_list = get_gpx_list(verbose);
        assert (test_gpx_list);
        int sensors_count = zlistx_size (test_gpx_list);
        assert (sensors_count == 1);
        // Test the first sensor
        _gpx_info_t *gpx_info = (_gpx_info_t *)zlistx_first (test_gpx_list);
        assert (gpx_info);
        // Modify the current_state
        gpx_info->current_state = GPIO_STATE_OPENED;

        // Send an update and check for the generated alert
        zstr_sendx (alerts, "UPDATE", endpoint, NULL);
        zclock_sleep (500);

        // Check the published alert
        zmsg_t *recv = mlm_client_recv (alerts_listener);
        assert (recv);
        fty_proto_t *frecv = fty_proto_decode (&recv);
        assert (frecv);
        assert (streq (fty_proto_name (frecv), "sensor-10"));
        assert (streq (fty_proto_state (frecv), "ACTIVE"));
        assert (streq (fty_proto_severity (frecv), "WARNING"));
        assert (streq (fty_proto_description (frecv), "Door has been opened"));

        fty_sensor_gpio_assets_destroy (&assets_self);
    }

    zactor_destroy (&alerts);
    mlm_client_destroy (&alerts_listener);
    zactor_destroy (&server);
    //  @end
    printf ("OK\n");
}
