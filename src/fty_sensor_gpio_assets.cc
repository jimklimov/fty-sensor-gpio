/*  =========================================================================
    fty_sensor_gpio_assets - 42ITy GPIO assets handler

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
    fty_sensor_gpio_assets - 42ITy GPIO assets handler
@discuss
@end
*/

#include "fty_sensor_gpio_classes.h"

//  Structure of our class

zlistx_t *_gpx_list = NULL;

struct _fty_sensor_gpio_assets_t {
    bool               verbose;       // is actor verbose or not
    char               *name;         // actor name
    mlm_client_t       *mlm;          // malamute client
    zlistx_t           *gpx_list;     // List of monitored GPx _gpx_info_t (10xGPI / 5xGPO on IPC3000)
};


//  --------------------------------------------------------------------------
//  Return a copy of the list of monitored sensors
zlistx_t *
get_gpx_list()
{
    zsys_debug ("%s", __func__);
    if (!_gpx_list)
        zsys_debug ("%s: GPx list not initialized, skipping", __func__);
    //return zlistx_dup (_gpx_list); 
    return _gpx_list; 
}

//  --------------------------------------------------------------------------
//  zlist handling -- destroy an item
void sensor_free(void **item)
{
    _gpx_info_t *gpx_info = (_gpx_info_t *)*item;

    if (!gpx_info)
        return;

    if (gpx_info->asset_name)
        free(gpx_info->asset_name);

    if (gpx_info->ext_name)
        free(gpx_info->ext_name);

    if (gpx_info->part_number)
        free(gpx_info->part_number);

    if (gpx_info->type)
        free(gpx_info->type);

    if (gpx_info->location)
        free(gpx_info->location);
    
    free(gpx_info);
}

//  --------------------------------------------------------------------------
//  zlist handling -- duplicate an item
static void *sensor_dup(const void *item)
{
    // Simply return item itself
    return (void*)item;
}

//  --------------------------------------------------------------------------
//  zlist handling - compare two items, for sorting
static int sensor_cmp(const void *item1, const void *item2)
{
    _gpx_info_t *gpx_info1 = (_gpx_info_t *)item1;
    _gpx_info_t *gpx_info2 = (_gpx_info_t *)item2;
    
    // Compare on asset_name
    if ( streq(gpx_info1->asset_name, gpx_info2->asset_name) )
        return 0;
    else
        return 1;
}

//  --------------------------------------------------------------------------
//  Sensors handling
//  Create a new empty structure
static
_gpx_info_t *sensor_new()
{
    _gpx_info_t *gpx_info = (_gpx_info_t *)malloc(sizeof(_gpx_info_t));
    if (!gpx_info) {
        zsys_debug ("ERROR: Can't allocate gpx_info!");
        return NULL;
    }

    gpx_info->asset_name = NULL;
    gpx_info->ext_name = NULL;
    gpx_info->part_number = NULL;
    gpx_info->type = NULL;
    gpx_info->location = NULL;
    gpx_info->normal_state = GPIO_STATE_UNKNOWN;
    gpx_info->current_state = GPIO_STATE_UNKNOWN;
    gpx_info->gpx_number = -1;
    gpx_info->gpx_direction = GPIO_DIRECTION_IN; // Default to GPI
    gpx_info->alarm_message = NULL;
    gpx_info->alarm_severity = NULL;

    return gpx_info;
}

//  --------------------------------------------------------------------------
//  Sensors handling
//  Add a new entry to our zlist of monitored sensors
/*static int
add_sensor(fty_sensor_gpio_assets_t *self, string config_template_filename, fty_proto_t *ftymessage)
static int
add_sensor(fty_sensor_gpio_assets_t *self, zconfig_t *sensor_config, fty_proto_t *ftymessage)
*/
static int
add_sensor(fty_sensor_gpio_assets_t *self,
    const char* assetname, const char* extname,
    const char* asset_subtype, const char* sensor_type,
    const char* sensor_normal_state, const char* sensor_gpx_number,
    const char* sensor_gpx_direction, const char* sensor_location,
    const char* sensor_alarm_message, const char* sensor_alarm_severity)
{
    // FIXME: check if already monitored! + sanity on < 10... AND pin not already declared/used

    _gpx_info_t *gpx_info = sensor_new();
    if (!gpx_info) {
        zsys_debug ("ERROR: Can't allocate gpx_info!");
        return 1;
    }

    gpx_info->asset_name = strdup(assetname);
    gpx_info->ext_name = strdup(extname);
    gpx_info->part_number = strdup(asset_subtype);
    gpx_info->type = strdup(sensor_type);
    if ( streq (sensor_normal_state, "opened" ) )
        gpx_info->normal_state = GPIO_STATE_OPENED;
    else if ( streq (sensor_normal_state, "closed") )
        gpx_info->normal_state = GPIO_STATE_CLOSED;
    gpx_info->gpx_number = atoi(sensor_gpx_number);
    if ( streq (sensor_gpx_direction, "GPO" ) )
        gpx_info->gpx_direction = GPIO_DIRECTION_OUT;
    else
        gpx_info->gpx_direction = GPIO_DIRECTION_IN;
    gpx_info->location = strdup(sensor_location);
    gpx_info->alarm_message = strdup(sensor_alarm_message);
    gpx_info->alarm_severity = strdup(sensor_alarm_severity);

    //if (zlistx_find (self->gpx_list, (void *) gpx_info) == NULL)
    if (zlistx_find (_gpx_list, (void *) gpx_info) == NULL)
        //zlistx_add_end (self->gpx_list, (void *) gpx_info);
        zlistx_add_end (_gpx_list, (void *) gpx_info);
    else {
        // else: check for updating fields
        zsys_debug ("Sensor '%s' is already monitored. Skipping!", assetname);
        return 0;
    }

    // Don't free gpx_info, it will be done a TERM time

    zsys_debug ("%s sensor '%s' (%s) added with\n\tmodel: %s\n\ttype: %s \
    \n\tnormal-state: %s\n\tPin number: %s\n\tlocation: %s \
    \n\talarm-message: %s\n\talarm-severity: %s",
        sensor_gpx_direction, extname, assetname, asset_subtype,
        sensor_type, sensor_normal_state, sensor_gpx_number, sensor_location,
        sensor_alarm_message, sensor_alarm_severity);

    return 0;
}

//  --------------------------------------------------------------------------
//  Sensors handling
//  Delete an entry from our zlist of monitored sensors
static int
delete_sensor(fty_sensor_gpio_assets_t *self, const char* assetname)
{
    _gpx_info_t *gpx_info_result = NULL;
    _gpx_info_t *gpx_info = sensor_new();

    if (gpx_info)
        gpx_info->asset_name = strdup(assetname);
    else {
        zsys_debug ("ERROR: Can't allocate gpx_info!");
        return 1;
    }

    gpx_info_result = (_gpx_info_t*)zlistx_find (self->gpx_list, (void *) gpx_info);
    sensor_free((void**)&gpx_info);

    if ( gpx_info_result == NULL )
        return 1;
    else {
        zsys_debug ("Deleting '%s'", assetname);
        // Delete from zlist
        zlistx_delete (self->gpx_list, (void *)gpx_info_result);
    }
    return 0;
}

//  --------------------------------------------------------------------------
//  Check if this asset is a GPIO sensor by
//  * Checking the provided subtype
//  * Checking for the existence of a template file according to the asset part
//    nb (provided in model)
//    If one exists, it's a GPIO sensor, so return the template filename
//    Otherwise, it's not a GPIO sensor, so return an empty string

static string
is_asset_gpio_sensor (string asset_subtype, string asset_model)
{
    string template_filename = "";

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

    // Check if a sensor template exists
    template_filename = string("./data/") + string(asset_model) + string(".tpl");
    FILE *template_file = fopen(template_filename.c_str(), "r");
    if (!template_file) {
        zsys_debug ("Template config file %s doesn't exist!", template_filename.c_str());
        zsys_debug ("Asset is not a GPIO sensor, skipping!");
    }
    else {
        zsys_debug ("Template config file %s found!", template_filename.c_str());
        zsys_debug ("Asset is a GPIO sensor, processing!");
        fclose(template_file);
        return template_filename;
    }

    return "";
}

//  --------------------------------------------------------------------------
//  When asset message comes, check if it is a GPIO sensor and store it or
//  update the monitoring structure.

// 2.2) fty-sensor-gpio listen to assets listing, filtering on
//      type=sensor and ext. attribute 'model' known in the supported catalog (data/<model>.tpl)
//      [and parent == self IPC?!]

static void
fty_sensor_gpio_handle_asset (fty_sensor_gpio_assets_t *self, fty_proto_t *ftymessage)
{
/* fty_asset_autoupdate.cc -> autoupdate_handle_message()
    if (!self || !message ) return;

    const char *sender = mlm_client_sender (self->client);
    const char *subj = mlm_client_subject (self->client);
    if (streq (sender, "asset-agent")) {
        if (streq (subj, "ASSETS_IN_CONTAINER")) {
            if ( self->verbose ) {
                zsys_debug ("%s:\tGot reply with RC:", self->name);
                zmsg_print (message);
            }
*/

    if (!self || !ftymessage) return;
    if (fty_proto_id (ftymessage) != FTY_PROTO_ASSET) return;

    zconfig_t *config_template = NULL;
    const char* operation = fty_proto_operation (ftymessage);
    const char* assetname = fty_proto_name (ftymessage);

    zsys_debug ("%s: '%s' operation on asset '%s'", __func__, operation, assetname);

    // Initial addition , listing or udpdate
    if ( (streq (operation, "inventory"))
        ||  (streq (operation, "create"))
        ||  (streq (operation, "update")) ) {

        const char* asset_subtype = fty_proto_ext_string (ftymessage, "subtype", "");
            // FIXME: fallback to "device.type"?
        const char* asset_model = fty_proto_ext_string (ftymessage, "model", "");
        string config_template_filename = is_asset_gpio_sensor(asset_subtype, asset_model);
        if (config_template_filename == "") {
            return;
        }
        // We have a GPIO sensor, process it
        config_template = zconfig_load (config_template_filename.c_str());
        if (!config_template) {
            zsys_debug ("Can't load sensor template file"); // FIXME: error
            return;
        }
        // Get static info from template
        const char *sensor_type = s_get (config_template, "type", "");
        const char *sensor_alarm_message = s_get (config_template, "alarm-message", "");
        // Get from user config
        const char *sensor_gpx_number = fty_proto_ext_string (ftymessage, "gpx_number", "");
        const char* extname = fty_proto_ext_string (ftymessage, "name", "");
        // Get normal state, direction and severity from user config, or fallback to template values
        const char *sensor_normal_state = s_get (config_template, "normal-state", "");
        sensor_normal_state = fty_proto_ext_string (ftymessage, "normal_state", sensor_normal_state);
        const char *sensor_gpx_direction = s_get (config_template, "gpx-direction", "GPI");
        sensor_gpx_direction = fty_proto_ext_string (ftymessage, "gpx_direction", sensor_gpx_direction);
        const char *sensor_location = fty_proto_ext_string (ftymessage, "location", "");
        const char *sensor_alarm_severity = s_get (config_template, "alarm-severity", "WARNING");
        sensor_alarm_severity = fty_proto_ext_string (ftymessage, "alarm_severity", sensor_alarm_severity);

        // Sanity checks
        if (streq (sensor_normal_state, "")) {
            zsys_debug ("No sensor normal state found in template nor provided by the user!");
            zsys_debug ("Skipping sensor");
            return;
        }
        if (streq (sensor_gpx_number, "")) {
            zsys_debug ("No sensor pin provided! Skipping sensor");
            return;
        }

        add_sensor( self, assetname, extname, asset_model,
                    sensor_type, sensor_normal_state,
                    sensor_gpx_number, sensor_gpx_direction, sensor_location,
                    sensor_alarm_message, sensor_alarm_severity);
    }
    // Asset deletion
    if (streq (operation, "delete")) {
        delete_sensor( self, assetname);
    }
}

//  --------------------------------------------------------------------------
//  Request all assets  from fty-asset, to init our monitoring structure.

void
request_sensor_assets(fty_sensor_gpio_assets_t *self)
{
    zsys_debug ("%s", __func__);

    if ( self->verbose)
        zsys_debug ("%s:\tRequest sensors list", self->name);
    zmsg_t *msg = zmsg_new ();
    zmsg_addstr (msg, "GET");
    zmsg_addstr (msg, "sensor");
    int rv = mlm_client_sendto (self->mlm, "asset-agent", "ASSETS", NULL, 5000, &msg);
    if (rv != 0) {
        zsys_error ("%s:\tRequest sensors list failed", self->name);
        zmsg_destroy (&msg);
    }
}

//  --------------------------------------------------------------------------
//  Create a new fty_sensor_gpio_assets

fty_sensor_gpio_assets_t *
fty_sensor_gpio_assets_new (const char* name)
{
    fty_sensor_gpio_assets_t *self = (fty_sensor_gpio_assets_t *) zmalloc (sizeof (fty_sensor_gpio_assets_t));
    assert (self);
    //  Initialize class properties here
    self->mlm         = mlm_client_new();
    self->name        = strdup(name);
    self->verbose     = false;
    // Declare our zlist for GPIOs tracking
    // Instanciated here and provided to all actors
#if 0
    self->gpx_list = _gpx_list;
    self->gpx_list = zlistx_new ();
    assert (self->gpx_list);
#endif
_gpx_list = zlistx_new ();
    // Declare zlist item handlers
    zlistx_set_duplicator (_gpx_list, (czmq_duplicator *) sensor_dup);
    zlistx_set_destructor (_gpx_list, (czmq_destructor *) sensor_free);
    zlistx_set_comparator (_gpx_list, (czmq_comparator *) sensor_cmp);
/*    zlistx_set_duplicator (self->gpx_list, (czmq_duplicator *) sensor_dup);
    zlistx_set_destructor (self->gpx_list, (czmq_destructor *) sensor_free);
    zlistx_set_comparator (self->gpx_list, (czmq_comparator *) sensor_cmp);
*/
    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the fty_sensor_gpio_assets

void
fty_sensor_gpio_assets_destroy (fty_sensor_gpio_assets_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        fty_sensor_gpio_assets_t *self = *self_p;
        //  Free class properties here
        zlistx_purge (self->gpx_list);
        zlistx_destroy (&self->gpx_list);
        free(self->name);
        mlm_client_destroy (&self->mlm);
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Create a new fty_sensor_gpio_assets

void
fty_sensor_gpio_assets (zsock_t *pipe, void *args)
{
    char *name = (char *)args;
    if (!name) {
        zsys_error ("Adress for fty-sensor-gpio actor is NULL");
        return;
    }

    fty_sensor_gpio_assets_t *self = fty_sensor_gpio_assets_new(name);
    assert (self);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (self->mlm), NULL);
    assert (poller);

    zsock_signal (pipe, 0); 
    zsys_info ("%s_assets: Started", self->name);

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
            zsys_debug ("fty-gpio-sensor-assets: received command %s", cmd);
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
                    zsys_debug("fty-gpio-sensor-assets: CONNECT %s/%s", endpoint, self->name);
                    zstr_free (&endpoint);
                }
                else if (streq (cmd, "PRODUCER")) {
                    char *stream = zmsg_popstr (message);
                    assert (stream);
                    mlm_client_set_producer (self->mlm, stream);
                    zsys_debug ("fty-gpio-sensor-assets: setting PRODUCER on %s", stream);
                    zstr_free (&stream);
                    request_sensor_assets(self);
                }
                else if (streq (cmd, "CONSUMER")) {
                    char *stream = zmsg_popstr (message);
                    char *pattern = zmsg_popstr (message);
                    assert (stream && pattern);
                    mlm_client_set_consumer (self->mlm, stream, pattern);
                    zsys_debug ("fty-gpio-sensor-assets: setting CONSUMER on %s/%s", stream, pattern);
                    zstr_free (&stream);
                    zstr_free (&pattern);
                }
                else if (streq (cmd, "VERBOSE")) {
                    self->verbose = true;
                    zsys_debug ("fty-gpio-sensor-assets: VERBOSE=true");
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
            zsys_debug ("===> Got a reply");
            zmsg_print (message);
            //autoupdate_handle_message (self, message);
            if (is_fty_proto (message)) {
                fty_proto_t *fmessage = fty_proto_decode (&message);
                if (fty_proto_id (fmessage) == FTY_PROTO_ASSET) {
                    fty_sensor_gpio_handle_asset (self, fmessage);
                }
                fty_proto_destroy (&fmessage);
                zmsg_destroy (&message);
            } else if (streq (mlm_client_command (self->mlm), "MAILBOX DELIVER")) {
                // someone is addressing us directly
                // FIXME: can be requested for
                // * sensors manifest (pn, type, normal status) by UI
                // => for _server?!
                // * what else?
                //s_handle_mailbox(self, message);
            }
        }
    }
    zpoller_destroy (&poller);
    mlm_client_destroy (&self->mlm);
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
fty_sensor_gpio_assets_test (bool verbose)
{
    printf (" * fty_sensor_gpio_assets: ");

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

    fty_sensor_gpio_assets_t *self = fty_sensor_gpio_assets_new (FTY_SENSOR_GPIO_AGENT"-assets");
    assert (self);
    fty_sensor_gpio_assets_destroy (&self);
    //  @end
    printf ("OK\n");
}
