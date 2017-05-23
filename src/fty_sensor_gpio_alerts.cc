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
    zsys_debug ("Publishing GPIO sensor %i (%s) alert",
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

    zsys_debug("%s: publishing alert %s with description:\n%s", __func__, rule.c_str (), description);
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
            zsys_debug("failed to send alert %s result %", topic.c_str(), r);
    }
    zmsg_destroy (&message);
}

//  --------------------------------------------------------------------------
//  Check GPIO status and generate alarms if needed

static void
s_check_gpio_status(fty_sensor_gpio_alerts_t *self)
{
    zsys_debug ("%s_alerts: %s", self->name, __func__);

    // number of sensors monitored in gpx_list
    zlistx_t *gpx_list = get_gpx_list();
    if (!gpx_list) {
        zsys_debug ("GPx list not initialized, skipping");
        return;
    }
    int sensors_count = zlistx_size (gpx_list);
    _gpx_info_t *gpx_info = NULL;

    if (sensors_count == 0) {
        zsys_debug ("No sensors monitored");
        return;
    }
    else
        zsys_debug ("%i sensor(s) monitored", sensors_count);

    if(!mlm_client_connected(self->mlm))
        return;

    // Acquire the current sensor
    gpx_info = (_gpx_info_t *)zlistx_first (gpx_list);

    // Loop on all sensors
    for (int cur_sensor_num = 0; cur_sensor_num < sensors_count; cur_sensor_num++) {

        // Status has been updated by the main server actor,
        // only check the fields

        // Check against normal state
        if (gpx_info->current_state != gpx_info->normal_state) {
            zsys_debug ("ALARM: state changed");
            // FIXME: do not repeat alarm?! so maybe flag in self
            publish_alert (self, gpx_info, 300);
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
            zsys_debug ("fty_sensor_gpio: received command %s", cmd);
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
                    zsys_debug("fty-gpio-sensor-alerts: CONNECT %s/%s", endpoint, self->name);
                    zstr_free (&endpoint);
                }
                else if (streq (cmd, "PRODUCER")) {
                    char *stream = zmsg_popstr (message);
                    assert (stream);
                    mlm_client_set_producer (self->mlm, stream);
                    zsys_debug ("fty-gpio-sensor-alerts: setting PRODUCER on %s", stream);
                    zstr_free (&stream);
                }
                else if (streq (cmd, "CONSUMER")) {
                    char *stream = zmsg_popstr (message);
                    char *pattern = zmsg_popstr (message);
                    assert (stream && pattern);
                    mlm_client_set_consumer (self->mlm, stream, pattern);
                    zsys_debug ("fty-gpio-sensor-alerts: setting CONSUMER on %s/%s", stream, pattern);
                    zstr_free (&stream);
                    zstr_free (&pattern);
                }
                else if (streq (cmd, "VERBOSE")) {
                    self->verbose = true;
                    zsys_debug ("fty_sensor_gpio: VERBOSE=true");
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

    fty_sensor_gpio_alerts_t *self = fty_sensor_gpio_alerts_new (FTY_SENSOR_GPIO_AGENT"-alerts");
    assert (self);
    fty_sensor_gpio_alerts_destroy (&self);
    //  @end
    printf ("OK\n");
}
