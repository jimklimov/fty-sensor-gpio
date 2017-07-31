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
//  https://gist.github.com/idada/11058000
//  * Copyright (C) 2010 chantra <chantra@debuntu.org>
//  * Copyright (C) 2012 Iain R. Learmonth <irl@sdf.org>
//  GPLv2+
static char*
str_replace(char* string, const char* substr, const char* replacement)
{
    char* tok = NULL;
    char* newstr = NULL;
    int   substr_len = 0;
    int   replacement_len = 0;

    newstr = strdup(string);

    if (substr == NULL || replacement == NULL) {
        return newstr;
    }
    substr_len = strlen(substr);
    replacement_len = strlen(replacement);

    while ((tok = strstr(newstr, substr))) {
        char* oldstr = newstr;
        int   oldstr_len = strlen(oldstr);
        newstr = (char*)malloc(sizeof(char) * (oldstr_len - substr_len + replacement_len + 1));

        if (newstr == NULL) {
            free(oldstr);
            return NULL;
        }

        memcpy(newstr, oldstr, tok - oldstr);
        memcpy(newstr + (tok - oldstr), replacement, replacement_len);
        memcpy(newstr + (tok - oldstr) + replacement_len, tok + substr_len, oldstr_len - substr_len - (tok - oldstr));
        memset(newstr + oldstr_len - substr_len + replacement_len, 0, 1);

        free(oldstr);
    }

    free(string);

    return newstr;
}

//  --------------------------------------------------------------------------
//  Publish an alert for the pointed GPIO sensor

int
publish_alert (fty_sensor_gpio_alerts_t *self, _gpx_info_t *sensor, int ttl, const char *state)
{
    my_zsys_debug (self->verbose, "Publishing GPIO sensor %i (%s) alert",
        sensor->gpx_number,
        sensor->asset_name);

    const char *severity = sensor->alarm_severity;
    char* description = strdup(sensor->alarm_message);

    // Adapt alarm message if needed
    if (strchr(description, '$')) {
        description = str_replace(description,
                                  "$status",
                                  libgpio_get_status_string(sensor->current_state).c_str());
        description = str_replace(description,
                                  "$device_name",
                                  sensor->ext_name);
        description = str_replace(description,
                                  "$location",
                                  sensor->location);
    }

    std::string rule = string(sensor->type) + ".state_change@" + sensor->location; //sensor->asset_name;

    my_zsys_debug (self->verbose, "%s: publishing alert %s with description:\n%s", __func__, rule.c_str (), description);
    zmsg_t *message = fty_proto_encode_alert(
        NULL,               // aux
        time (NULL),        // timestamp
        ttl,
        rule.c_str (),      // rule
        sensor->location,   // element
        state,              // state
        severity,           // severity
        description,        // description
        ""                  // action ?email
    );
    std::string topic = rule + "/" + severity + "@" + sensor->asset_name;
    int r=-1;
    if (message) {
        r = mlm_client_send (self->mlm, topic.c_str (), &message);
        if( r != 0 )
            zsys_error ("failed to send alert %s result %d", topic.c_str(), r);
    }
    zmsg_destroy (&message);
    zstr_free(&description);
    return r;
}

//  --------------------------------------------------------------------------
//  Check GPIO status and generate alarms if needed

static void
s_check_gpio_status(fty_sensor_gpio_alerts_t *self)
{
    my_zsys_debug (self->verbose, "%s_alerts: %s", self->name, __func__);

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

        // Status has been updated by the main server actor,
        // only check the fields

        // No processing if not yet init'ed, or GPO!
        if ( (gpx_info) && (gpx_info->gpx_direction != GPIO_DIRECTION_OUT)
            && (gpx_info->current_state != GPIO_STATE_UNKNOWN) )
        {
            // Check against normal state
            if (gpx_info->current_state != gpx_info->normal_state ) {
                my_zsys_debug (self->verbose, "ALARM: state changed -> ACTIVE");
                int rv = publish_alert (self, gpx_info, 300, "ACTIVE");
                if( rv == 0 )
                    gpx_info->alert_triggered=true; // Alert triggered
            }
            if (gpx_info->current_state == gpx_info->normal_state && 
                    gpx_info->alert_triggered) {
                my_zsys_debug (self->verbose, "ALARM: state changed -> RESOLVED");
                int rv = publish_alert (self, gpx_info, 300, "RESOLVED");
                if( rv == 0 )
                    gpx_info->alert_triggered=false; // Alert reset triggered
            }
        }
        gpx_info = (_gpx_info_t *)zlistx_next (gpx_list);
    }
    pthread_mutex_unlock (&gpx_list_mutex);
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
//  Destroy the fty_sensor_gpio_alerts

void
fty_sensor_gpio_alerts_destroy (fty_sensor_gpio_alerts_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        fty_sensor_gpio_alerts_t *self = *self_p;
        //  Free class properties
        mlm_client_destroy (&self->mlm);
        if (self->name)
            zstr_free (&self->name);
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
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
            if (cmd) {
                my_zsys_debug (self->verbose, "fty_sensor_gpio: received command %s", cmd);
                if (streq (cmd, "$TERM")) {
                    zstr_free (&cmd);
                    zmsg_destroy (&message);
                    goto exit;
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
        /* else if (which == mlm_client_msgpipe (self->mlm)) {
        }*/
    }
exit:
    zpoller_destroy (&poller);
    fty_sensor_gpio_alerts_destroy(&self);
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
            "Eaton", "sensorgpio-10", "GPIO-Sensor-Door1",
            "DCS001", "door-contact-sensor",
            "closed", "1",
            "GPI", "IPC1", "Rack1", "",
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
        pthread_mutex_unlock (&gpx_list_mutex);

        // Send an update and check for the generated alert
        zstr_sendx (alerts, "UPDATE", endpoint, NULL);
        zclock_sleep (500);

        // Check the published alert
        zmsg_t *recv = mlm_client_recv (alerts_listener);
        assert (recv);
        fty_proto_t *frecv = fty_proto_decode (&recv);
        assert (frecv);
        assert (streq (fty_proto_name (frecv), "Rack1"));
        assert (streq (fty_proto_state (frecv), "ACTIVE"));
        assert (streq (fty_proto_severity (frecv), "WARNING"));
        assert (streq (fty_proto_description (frecv), "Door has been opened"));

        fty_proto_destroy (&frecv);
        zmsg_destroy (&recv);
        fty_sensor_gpio_assets_destroy (&assets_self);
    }

    mlm_client_destroy (&alerts_listener);
    zactor_destroy (&alerts);
    zactor_destroy (&server);
    //  @end
    printf ("OK\n");
}
