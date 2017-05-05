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
@end
*/

#include "fty_sensor_gpio_classes.h"

//  Structure of our class

struct _fty_sensor_gpio_server_t {
    bool               verbose;      // is actor verbose or not
    char               *name;        // actor name
    mlm_client_t       *mlm;         // malamute client
    int                sensor_num;   // number of sensors monitored in gpi_list
    struct _gpi_info_t gpi_list[10]; // Array of monitored GPI (10 GPI on IPC3000)
};

// TODO: get from config
#define TIMEOUT_MS -1   //wait infinitelly


char*
s_get (zconfig_t *config, const char* key, std::string &dfl) {
    assert (config);
    char *ret = zconfig_get (config, key, dfl.c_str());
    if (!ret || streq (ret, ""))
        return (char*)dfl.c_str();
    return ret;
}

char*
s_get (zconfig_t *config, const char* key, char*dfl) {
    assert (config);
    char *ret = zconfig_get (config, key, dfl);
    if (!ret || streq (ret, ""))
        return dfl;
    return ret;
}

//  --------------------------------------------------------------------------
//  Create a new fty_sensor_gpio_server

fty_sensor_gpio_server_t *
fty_sensor_gpio_server_new (const char* name)
{
    fty_sensor_gpio_server_t *self = (fty_sensor_gpio_server_t *) zmalloc (sizeof (fty_sensor_gpio_server_t));
    assert (self);
    //  Initialize class properties here
    self->mlm  = mlm_client_new();
    self->name = strdup(name);
    self->sensor_num = 0;

    for (int i = 0; i < 10; i++) {
//        self->gpi_list[i].name = "";
//        self->gpi_list[i].part_number = "";
//        self->gpi_list[i].type = "";
        self->gpi_list[i].normal_state = GPIO_STATUS_UNKNOWN;
        self->gpi_list[i].current_state = GPIO_STATUS_UNKNOWN;
        self->gpi_list[i].gpi_number = -1;
    }

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
        //  Free class properties here
        mlm_client_destroy (&self->mlm);
        free(self->name);
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Check for the existence of a template file according to the asset part nb
//  If one exists, it's a GPIO sensor, so return the template filename
//  Otherwise, it's not a GPIO sensor, so return an empty string

static string
is_asset_gpio_sensor (string asset_subtype, string asset_model)
{
    zconfig_t *config_template = NULL;
    string template_file = "";

    if ((asset_subtype == "") || (asset_subtype == "N_A")) {
        zsys_debug ("Asset subtype is not available");
        zsys_debug ("Verification will be limited to template existence!");
    }
    else {
        // Check if it's a sensor, otherwise no need to continue!
        if (asset_subtype != "sensor") {
            zsys_debug ("Asset is not a sensor, skipping!");
            return "";
        }
    }

    if (asset_model == "")
        return "";

    // Acquire sensor template info (type, default-state, alarm-message)
    // FIXME: open $datadir/<sensor_partnumber>.tpl
    template_file = string("./data/") + string(asset_model) + string(".tpl");
    // FIXME: simply test file existence, no zconfig_load...
    config_template = zconfig_load (template_file.c_str());
    if (!config_template) {
        zsys_debug ("Template config file %s doesn't exist!", template_file.c_str());
        zsys_debug ("Asset is not a GPIO sensor, skipping!");
    }
    else {
        zsys_debug ("Template config file %s found!", template_file.c_str());
        zsys_debug ("Asset is a GPIO sensor, processing!");
        return template_file;
    }

    return "";
}

//  --------------------------------------------------------------------------
//  When asset message comes, check if it is a GPIO sensor and store it or
// update the monitoring structure.

// 2.2) fty-sensor-gpio listen to assets listing, filtering on
//      type=sensor and ext. attribute 'model' known in the supported catalog (data/<model>.tpl)
//      [and parent == self IPC?!]

static void
fty_sensor_gpio_handle_asset (fty_sensor_gpio_server_t *self, fty_proto_t *ftymsg)
{
    if (!self || !ftymsg) return;
    if (fty_proto_id (ftymsg) != FTY_PROTO_ASSET) return;

    zconfig_t *config_template = NULL;
    const char *operation = fty_proto_operation (ftymsg);
    const char *assetname = fty_proto_name (ftymsg);

    zsys_debug ("%s: '%s' operation on asset '%s'", __func__, operation, assetname);

    const char* asset_subtype = fty_proto_ext_string (ftymsg, "subtype", "");
        // FIXME: fallback to "device.type"?
    const char* asset_model = fty_proto_ext_string (ftymsg, "model", "");
    string config_template_filename = is_asset_gpio_sensor(asset_subtype, asset_model);

    if (config_template_filename != "") {
        // We have a GPIO sensor, process it
        config_template = zconfig_load (config_template_filename.c_str());
        if (config_template) {
            // Get normal state and type from template
            char* template_normal_state = s_get (config_template, "normal-state",  NULL);
            char* sensor_type = s_get (config_template, "type",  NULL);
            const char* sensor_normal_state = fty_proto_ext_string (ftymsg, "normal_state", template_normal_state);
            const char* sensor_gpi_pin = fty_proto_ext_string (ftymsg, "gpi_number", "");
            if (!sensor_normal_state) {
                zsys_debug ("No sensor normal state found in template nor provided by the user!");
                zsys_debug ("Skipping sensor");
            }
            else if (!sensor_gpi_pin) {
                zsys_debug ("No sensor pin provided! Skipping sensor");
            }
            else {
                // Initial addition
                if (streq (operation, "inventory")) {
                    // FIXME: check if already monitored! + sanity on < 10...
                    self->sensor_num++;
                    // FIXME: add_sensor(assetname, asset_subtype, sensor_type, normal_state, gpi_number)
                    self->gpi_list[self->sensor_num].name = strdup(assetname);
                    self->gpi_list[self->sensor_num].part_number = asset_subtype;
                    self->gpi_list[self->sensor_num].type = sensor_type;
                    if (sensor_normal_state == string("opened"))
                        self->gpi_list[self->sensor_num].normal_state = GPIO_STATUS_OPENED;
                    else if (sensor_normal_state == string("closed"))
                        self->gpi_list[self->sensor_num].normal_state = GPIO_STATUS_CLOSED;
                    self->gpi_list[self->sensor_num].gpi_number = atoi(sensor_gpi_pin);
                    zsys_debug ("Sensor %s added with\n\tmodel: %s\n\ttype:%s\n\tnormal-state: %s\n\tPin number: %s",
                        assetname, asset_subtype, sensor_type, sensor_normal_state, sensor_gpi_pin);
                }
                // Asset deletion
                if (streq (operation, "delete")) {
                    // FIXME: del_sensor(assetname)
                    ;
                }
                // Asset update
                if (streq (operation, "update")) {
                    // FIXME: update_sensor(assetname)
                    ;
                }
            }
        }
    }
}

/*
static int
s_poll_assets_info(fty_sensor_gpio_server_t *self)
{

    return 0;
}
*/

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
    zsys_info ("fty-sensor-gpio: Started");

    while (!zsys_interrupted)
    {
        void *which = zpoller_wait (poller, TIMEOUT_MS);
        if (which == NULL) {
            if (zpoller_terminated (poller) || zsys_interrupted) {
                break;
            }
        }
        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            char *cmd = zmsg_popstr (msg);
            zsys_debug ("fty_sensor_gpio: received command %s", cmd);
            if (cmd) {
                if (streq (cmd, "$TERM")) {
                    zstr_free (&cmd);
                    zmsg_destroy (&msg);
                    break;
                }
                else if (streq (cmd, "CONNECT")) {
                    char *endpoint = zmsg_popstr (msg);
                     if (!endpoint)
                        zsys_error ("%s:\tMissing endpoint", self->name);
                    assert (endpoint);
                    int r = mlm_client_connect (self->mlm, endpoint, 5000, self->name);
                    if (r == -1)
                        zsys_error ("%s:\tConnection to endpoint '%s' failed", self->name, endpoint);
                    zsys_debug("fty-gpio-sensor-server: CONNECT %s/%s",endpoint,self->name);
                    zstr_free (&endpoint);
                }
                else if (streq (cmd, "PRODUCER")) {
                    char *stream = zmsg_popstr (msg);
                    assert (stream);
                    mlm_client_set_producer (self->mlm, stream);
                    zsys_debug ("fty_sensor_gpio: setting PRODUCER on %s", stream);
                    zstr_free (&stream);
                }
                else if (streq (cmd, "CONSUMER")) {
                    char *stream = zmsg_popstr (msg);
                    char *pattern = zmsg_popstr (msg);
                    assert (stream && pattern);
                    mlm_client_set_consumer (self->mlm, stream, pattern);
                    zsys_debug ("fty_sensor_gpio: setting CONSUMER on %s/%s", stream, pattern);
                    zstr_free (&stream);
                    zstr_free (&pattern);
                }
                else
                    zsys_warning ("%s:\tUnknown API command=%s, ignoring", __func__, cmd);

                zstr_free (&cmd);
            }
            zmsg_destroy (&msg);
        }
        else if (which == mlm_client_msgpipe (self->mlm)) {
            zmsg_t *msg = mlm_client_recv (self->mlm);
            if (is_fty_proto (msg)) {
                fty_proto_t *fmsg = fty_proto_decode (&msg);
                if (fty_proto_id (fmsg) == FTY_PROTO_ASSET) {
                    fty_sensor_gpio_handle_asset (self, fmsg);
                }
                /*if (fty_proto_id (fmsg) == FTY_PROTO_METRIC) {
                    flexible_alert_handle_metric (self, &fmsg);
                }*/
                fty_proto_destroy (&fmsg);
            } else if (streq (mlm_client_command (self->mlm), "MAILBOX DELIVER")) {
                // someone is addressing us directly
            }
        }
    }
    zpoller_destroy (&poller);
    mlm_client_destroy (&self->mlm);
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
fty_sensor_gpio_server_test (bool verbose)
{
    printf (" * fty_sensor_gpio_server: ");

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
