/*  =========================================================================
    fty_sensor_gpio_assets - 42ITy GPIO assets handler

    Copyright (C) 2014 - 2020 Eaton

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

// List of monitored GPx
zlistx_t *_gpx_list = NULL;
// GPx list protection mutex
pthread_mutex_t gpx_list_mutex = PTHREAD_MUTEX_INITIALIZER;

//  Structure of our class

struct _fty_sensor_gpio_assets_t {
    char               *name;         // actor name
    mlm_client_t       *mlm;          // malamute client
    zlistx_t           *gpx_list;     // List of monitored GPx _gpx_info_t (10xGPI / 5xGPO on IPC3000)
    char               *template_dir; // Location of the template files
    bool               test_mode;     // true if we are in test mode, false otherwise
};


//  --------------------------------------------------------------------------
//  Return the list of monitored sensors without transfering ownership

zlistx_t *
get_gpx_list()
{
    if (!_gpx_list)
        log_debug ("GPx list not initialized, skipping");
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

    if (gpx_info->manufacturer)
        free(gpx_info->manufacturer);

    if (gpx_info->asset_name)
        free(gpx_info->asset_name);

    if (gpx_info->ext_name)
        free(gpx_info->ext_name);

    if (gpx_info->part_number)
        free(gpx_info->part_number);

    if (gpx_info->type)
        free(gpx_info->type);

    if (gpx_info->parent)
        free(gpx_info->parent);

    if (gpx_info->location)
        free(gpx_info->location);

    if (gpx_info->power_source)
        free(gpx_info->power_source);

    if (gpx_info->alarm_message)
        free(gpx_info->alarm_message);

    if (gpx_info->alarm_severity)
        free(gpx_info->alarm_severity);

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
        log_error ("Can't allocate gpx_info!");
        return NULL;
    }

    gpx_info->manufacturer = NULL;
    gpx_info->asset_name = NULL;
    gpx_info->ext_name = NULL;
    gpx_info->part_number = NULL;
    gpx_info->type = NULL;
    gpx_info->parent = NULL;
    gpx_info->location = NULL;
    gpx_info->normal_state = GPIO_STATE_UNKNOWN;
    gpx_info->current_state = GPIO_STATE_UNKNOWN;
    gpx_info->gpx_number = -1;
    gpx_info->pin_number = -1;
    gpx_info->gpx_direction = GPIO_DIRECTION_IN; // Default to GPI
    gpx_info->power_source = NULL;
    gpx_info->alarm_message = NULL;
    gpx_info->alarm_severity = NULL;
    gpx_info->alert_triggered = false;

    return gpx_info;
}

//  --------------------------------------------------------------------------
//  Sensors handling
//  Add a new entry to our zlist of monitored sensors
//  Returns 1 on error, 0 otherwise

//static
int
add_sensor(fty_sensor_gpio_assets_t *self, const char* operation,
    const char* manufacturer, const char* assetname, const char* extname,
    const char* asset_subtype, const char* sensor_type,
    const char* sensor_normal_state, const char* sensor_gpx_number,
    const char* sensor_gpx_direction, const char* sensor_parent,
    const char* sensor_location, const char* sensor_power_source,
    const char* sensor_alarm_message, const char* sensor_alarm_severity)
{
    int gpx_number = atoi(sensor_gpx_number);
    // FIXME: libgpio should be shared with -asset too
    // Sanity check on the sensor_gpx_number Vs number of supported (count)
    if (!self->test_mode) {
        if ( streq (sensor_gpx_direction, "GPO" ) ) {
            if (gpx_number > libgpio_get_gpo_count ()) {
                log_error ("GPO number is higher than the number of supported GPO");
                return 1;
            }
        }
        else {
            if (gpx_number > libgpio_get_gpi_count ()) {
                log_error ("GPI number is higher than the number of supported GPI");
                return 1;
            }
        }
    }
    _gpx_info_t *prev_gpx_info = NULL;
    _gpx_info_t *gpx_info = sensor_new();
    if (!gpx_info) {
        log_error ("Can't allocate gpx_info!");
        return 1;
    }

    gpx_info->manufacturer = strdup(manufacturer);
    gpx_info->asset_name = strdup(assetname);
    gpx_info->ext_name = strdup(extname);
    gpx_info->part_number = strdup(asset_subtype);
    gpx_info->type = strdup(sensor_type);
    if (libgpio_get_status_value (sensor_normal_state) != GPIO_STATE_UNKNOWN)
        gpx_info->normal_state = libgpio_get_status_value (sensor_normal_state);
    else {
        log_error ("provided normal_state '%s' is not valid!", sensor_normal_state);
        return 1;
    }
    gpx_info->gpx_number = gpx_number;
//    gpx_info->pin_number = atoi(sensor_pin_number);
    if ( streq (sensor_gpx_direction, "GPO" ) ) {
        gpx_info->gpx_direction = GPIO_DIRECTION_OUT;
        // GPO status can be init'ed with default closed?!
        // current_state = GPIO_STATE_CLOSED;
    }
    else {
        gpx_info->gpx_direction = GPIO_DIRECTION_IN;
    }
    if (sensor_parent)
        gpx_info->parent = strdup(sensor_parent);
    if (sensor_location)
        gpx_info->location = strdup(sensor_location);
    // Note: If there is a GPO power source, -server will enable
    // it in the next status update loop...
    if (sensor_power_source)
        gpx_info->power_source = strdup(sensor_power_source);
    if (sensor_alarm_message)
        gpx_info->alarm_message = strdup(sensor_alarm_message);
    if (sensor_alarm_severity)
        gpx_info->alarm_severity = strdup(sensor_alarm_severity);

    pthread_mutex_lock (&gpx_list_mutex);

    // Check for an already existing entry for this asset
    prev_gpx_info = (_gpx_info_t*)zlistx_find (_gpx_list, (void *) gpx_info);

    if ( prev_gpx_info != NULL) {
        // In case of update, we remove the previous entry, and create a new one
        if ( streq (operation, "update" ) ) {
            // FIXME: we may lose some data, check for merging entries prior to deleting
            if (zlistx_delete (_gpx_list, (void *)prev_gpx_info) == -1) {
                log_error ("Update: error deleting the previous GPx record for '%s'!", assetname);
                pthread_mutex_unlock (&gpx_list_mutex);
                return -1;
            }
        }
        else {
            log_debug ("Sensor '%s' is already monitored. Skipping!", assetname);
            pthread_mutex_unlock (&gpx_list_mutex);
            return 0;
        }
    }
    zlistx_add_end (_gpx_list, (void *) gpx_info);

    pthread_mutex_unlock (&gpx_list_mutex);

    // Don't free gpx_info, it will be done at TERM time

    log_debug ("%s sensor '%s' (%s) %sd with\n\tmanufacturer: %s\n\tmodel: %s \
    \n\ttype: %s\n\tnormal-state: %s\n\t%s number: %s\n\tparent: %s\n\tlocation: %s \
    \n\tpower source: %s\n\talarm-message: %s\n\talarm-severity: %s",
        sensor_gpx_direction, extname, assetname, operation, manufacturer, asset_subtype,
        sensor_type, sensor_normal_state, sensor_gpx_direction, sensor_gpx_number, sensor_parent,
        sensor_location, sensor_power_source, sensor_alarm_message, sensor_alarm_severity);

    return 0;
}

//  --------------------------------------------------------------------------
//  Sensors handling
//  Delete an entry from our zlist of monitored sensors
static int
delete_sensor(fty_sensor_gpio_assets_t *self, const char* assetname)
{
    int retval = 0;

    // We need to find out GPx direction
    // And we cannot use gpx_info_result because it contains garbage on `gpx_direction` position
    _gpx_info_t *info = (_gpx_info_t *)zlistx_first (_gpx_list);
    while (info != NULL) {
        char *info_name = info->asset_name;
        if (streq (info_name, assetname)) {
            int direction = info->gpx_direction;
            if (direction ==  GPIO_DIRECTION_OUT) {
                zmsg_t *request = zmsg_new ();
                zmsg_addstr (request, assetname);
                zmsg_addstr (request, "-1");
                mlm_client_sendto (self->mlm, FTY_SENSOR_GPIO_AGENT, "GPOSTATE", NULL, 1000, &request);
            }
            break;
        }
        info = (_gpx_info_t *)zlistx_next (_gpx_list);
    }

    _gpx_info_t *gpx_info_result = NULL;
    _gpx_info_t *gpx_info = sensor_new();

    if (gpx_info)
        gpx_info->asset_name = strdup(assetname);
    else {
        log_error ("Can't allocate gpx_info!");
        return 1;
    }

    pthread_mutex_lock (&gpx_list_mutex);

    gpx_info_result = (_gpx_info_t*)zlistx_find (_gpx_list, (void *) gpx_info);
    sensor_free((void**)&gpx_info);

    if ( gpx_info_result == NULL ) {
        retval = 1;
    }
    else {
        log_debug ("Deleting '%s'", assetname);
        // Delete from zlist
        zlistx_delete (_gpx_list, (void *)gpx_info_result);
    }
    pthread_mutex_unlock (&gpx_list_mutex);
    return retval;
}

//  --------------------------------------------------------------------------
//  Check if this asset is a GPIO sensor by
//  * Checking the provided subtype
//  * Checking for the existence of a template file according to the asset part
//    nb (provided in model)
//    If one exists, it's a GPIO sensor, so return the template filename
//    Otherwise, it's not a GPIO sensor, so return an empty string

static string
is_asset_gpio_sensor (fty_sensor_gpio_assets_t *self, string asset_subtype, string asset_model)
{
    string template_filename = "";

    if ((asset_subtype == "") || (asset_subtype == "N_A")) {
        log_debug ("Asset subtype is not available");
        log_debug ("Verification will be limited to template existence!");
    }
    else {
        // Check if it's a sensor, otherwise no need to continue!
        if (asset_subtype != "sensorgpio" && asset_subtype != "gpo") {
            log_debug ("Asset is not a GPIO sensor, skipping!");
            return "";
        }
    }

    if (asset_model == "")
        return "";

    // Check if a sensor template exists
    template_filename = string(self->template_dir) + string(asset_model) + string(".tpl");

    FILE *template_file = fopen(template_filename.c_str(), "r");
    if (!template_file) {
        log_debug ("Template config file %s doesn't exist!", template_filename.c_str());
        log_debug ("Asset is not a GPIO sensor, skipping!");
    }
    else {
        log_debug ("Template config file %s found!", template_filename.c_str());
        log_debug ("Asset is a GPIO sensor, processing!");
        fclose(template_file);
        return template_filename;
    }

    return "";
}

//  --------------------------------------------------------------------------
//  When asset message comes, check if it is a GPIO sensor and store it or
//  update the monitoring structure.

static void
fty_sensor_gpio_handle_asset (fty_sensor_gpio_assets_t *self, fty_proto_t *ftymessage)
{
    if (!self || !ftymessage) return;
    if (fty_proto_id (ftymessage) != FTY_PROTO_ASSET) return;

    zconfig_t *config_template = NULL;
    const char* operation = fty_proto_operation (ftymessage);
    const char* assetname = fty_proto_name (ftymessage);

    log_debug ("'%s' operation on asset '%s'", operation, assetname);

    // Initial addition , listing or udpdate
    if ( (streq (operation, "inventory"))
        ||  (streq (operation, "create"))
        ||  (streq (operation, "update")) ) {
        const char* asset_subtype = fty_proto_aux_string (ftymessage, "subtype", "");
        log_debug ("asset_subtype = %s", asset_subtype);

        // Only consider active assets!
        // Default to 'active' for inventory
        if (!streq (fty_proto_aux_string (ftymessage, "status", "active"), "active")) {
            // when updating from active to inactive, delete the sensor
            if (streq (operation, "update")) {
                log_debug ("%s: deleting de-actived sensor %s", self->name, assetname);
                delete_sensor( self, assetname);
            }
            else
                log_debug ("%s: discarded non active sensor %s", self->name, assetname);

            return;
        }

        if (streq (asset_subtype, "sensorgpio")) {
            const char* asset_model = fty_proto_ext_string (ftymessage, "model", "");

            string config_template_filename = is_asset_gpio_sensor(self, asset_subtype, asset_model);
            if (config_template_filename == "") {
                return;
            }

            const char *asset_parent_name1 = fty_proto_aux_string (ftymessage, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "");
            if (0 != strncmp ("rackcontroller", asset_parent_name1, strlen ("rackcontroller"))) {
                // This agent should handle only local sensors
                return;
            }

            // We have a GPI sensor, process it
            config_template = zconfig_load (config_template_filename.c_str());
            if (!config_template) {
                log_debug ("Can't load sensor template file"); // FIXME: error
                return;
            }

            // Get static info from template
            const char *manufacturer = s_get (config_template, "manufacturer", "");
            const char *sensor_type = s_get (config_template, "type", "");
            // FIXME: can come from user config
            const char *sensor_alarm_message = s_get (config_template, "alarm-message", "");
            // Get from user config
            const char *sensor_gpx_number = fty_proto_ext_string (ftymessage, "port", "");
            const char* extname = fty_proto_ext_string (ftymessage, "name", "");
            // Get normal state, direction and severity from user config, or fallback to template values
            const char *sensor_normal_state = s_get (config_template, "normal-state", "");
            sensor_normal_state = fty_proto_ext_string (ftymessage, "normal_state", sensor_normal_state);
            const char *sensor_gpx_direction = s_get (config_template, "gpx-direction", "GPI");
            sensor_gpx_direction = fty_proto_ext_string (ftymessage, "gpx_direction", sensor_gpx_direction);
            // And deployment location
            const char *sensor_location = fty_proto_ext_string (ftymessage, "logical_asset", "");
            const char *sensor_alarm_severity = s_get (config_template, "alarm-severity", "WARNING");
            sensor_alarm_severity = fty_proto_ext_string (ftymessage, "alarm_severity", sensor_alarm_severity);
            // Get the GPO which power us
            const char* power_source = fty_proto_ext_string (ftymessage, "gpo_powersource", "");
            // FIXME: need a power topology request, for latter expansion
            request_sensor_power_source(self, assetname);

            // Sanity checks
            if (streq (sensor_normal_state, "")) {
                log_debug ("No sensor normal state found in template nor provided by the user!");
                log_debug ("Skipping sensor");
                zconfig_destroy (&config_template);
                return;
            }
            if (streq (sensor_gpx_number, "")) {
                log_debug ("No sensor pin (port) provided! Skipping sensor");
                zconfig_destroy (&config_template);
                return;
            }

            add_sensor( self, operation,
                        manufacturer, assetname, extname, asset_model,
                        sensor_type, sensor_normal_state,
                        sensor_gpx_number, sensor_gpx_direction, asset_parent_name1,
                        sensor_location, power_source, sensor_alarm_message, sensor_alarm_severity);

            zconfig_destroy (&config_template);
        }
        if (streq (asset_subtype, "gpo")) {
            const char *asset_parent_name1 = fty_proto_aux_string (ftymessage, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "");
            if (0 != strncmp ("rackcontroller", asset_parent_name1, strlen ("rackcontroller"))) {
                // This agent should handle only local sensors
                return;
            }
            const char *sensor_gpx_number = fty_proto_ext_string (ftymessage, "port", "");
            const char* extname = fty_proto_ext_string (ftymessage, "name", "");
            const char *sensor_normal_state = fty_proto_ext_string (ftymessage, "normal_state", "closed");
            const char *sensor_gpx_direction = fty_proto_ext_string (ftymessage, "gpx_direction", "GPO");

            // Sanity checks
            if (streq (sensor_normal_state, "")) {
                log_debug ("No sensor normal state found in template nor provided by the user!");
                log_debug ("Skipping sensor");
                zconfig_destroy (&config_template);
                return;
            }
            if (streq (sensor_gpx_number, "")) {
                log_debug ("No sensor pin (port) provided! Skipping sensor");
                zconfig_destroy (&config_template);
                return;
            }

            zmsg_t *request = zmsg_new ();
            zmsg_addstr (request, assetname);
            zmsg_addstr (request, sensor_gpx_number);
            zmsg_addstr (request, sensor_normal_state);
            mlm_client_sendto (self->mlm, FTY_SENSOR_GPIO_AGENT, "GPOSTATE", NULL, 1000, &request);

            add_sensor( self, operation,
                        "", assetname, extname, "",
                        "", sensor_normal_state,
                        sensor_gpx_number, sensor_gpx_direction, asset_parent_name1,
                        "", "", "", "");

        }
    }
    // Asset deletion
    if (streq (operation, "delete")) {
        delete_sensor( self, assetname);
    }
}

//  --------------------------------------------------------------------------
//  Request the power source of a given 'sensorgpio' assets

void
request_sensor_power_source(fty_sensor_gpio_assets_t *self, const char* asset_name)
{
// FIXME: need a power topology request:
/*         subject: "TOPOLOGY"
         message: is a multipart message A/B
                 A = "TOPOLOGY_POWER" - mandatory
                 B = "asset_name" - mandatory
*/
}

//  --------------------------------------------------------------------------
//  Request all 'sensorgpio' assets  from fty-asset, to init our monitoring
//  structure.

void
request_sensor_assets(fty_sensor_gpio_assets_t *self)
{
    log_debug ("%s:\tRequest GPIO sensors list", self->name);

    zmsg_t *msg = zmsg_new ();
    zuuid_t *uuid = zuuid_new ();
    zmsg_addstr (msg, "GET");
    zmsg_addstr (msg, zuuid_str_canonical (uuid));
    zmsg_addstr (msg, "gpo");
    zmsg_addstr (msg, "sensorgpio");

    int rv = mlm_client_sendto (self->mlm, "asset-agent", "ASSETS", NULL, 5000, &msg);
    if (rv != 0)
        log_error ("%s:\tRequest GPIO sensors list failed", self->name);
    else
        log_debug ("%s:\tGPIO sensors list request sent successfully", self->name);
    zmsg_destroy (&msg);

    zmsg_t *reply = mlm_client_recv (self->mlm);
    if (!reply)
        log_error ("%s: no reply message received", self->name);

    char *uuid_recv = zmsg_popstr(reply);

    if (0 != strcmp (zuuid_str_canonical (uuid), uuid_recv)) {
        log_debug ("%s:\tGPIO zuuid doesn't match 1", self->name);
        zmsg_destroy (&reply);
        zstr_free (&uuid_recv);
    }

    if (streq (zmsg_popstr (reply), "ERROR"))
    {
        char *reason = zmsg_popstr (reply);
        log_error ("%s: error message received %s", self->name, reason);
    }

    char *asset = NULL;
    asset = zmsg_popstr(reply);

    while (asset)
    {
        uuid = zuuid_new ();
        msg = zmsg_new ();
        zmsg_addstr (msg, "GET");
        zmsg_addstr (msg, zuuid_str_canonical (uuid));
        zmsg_addstr (msg, asset);

        rv = mlm_client_sendto (self->mlm, "asset-agent", "ASSET_DETAIL", NULL, 5000, &msg);
        if (rv != 0)
            log_error ("%s:\tRequest ASSET_DETAIL failed for %s", self->name, asset);

        log_debug ("sending ASSET_DETAIL request");
        zmsg_t *reply2 = mlm_client_recv (self->mlm);

        if (reply2)
        {
            char *uuid_recv = zmsg_popstr (reply2);

            if (0 != strcmp (zuuid_str_canonical (uuid), uuid_recv)) {
                log_debug ("%s:\tGPIO zuuid doesn't match 2", self->name);
                zmsg_destroy (&reply2);
                continue;
            }

            if (fty_proto_is (reply2))
            {
                fty_proto_t *fmessage = fty_proto_decode (&reply2);
                if (fty_proto_id (fmessage) == FTY_PROTO_ASSET)
                {
                    log_debug ("%s: Processing sensor %s", self->name, asset);
                    fty_proto_print (fmessage);

                    fty_sensor_gpio_handle_asset (self, fmessage);
                }
                fty_proto_destroy (&fmessage);
            }
            else
            {
                if (streq (zmsg_popstr (reply2), "ERROR"))
                    log_debug ("%s: error received %s", self->name, zmsg_popstr (reply2));
            }
            asset = zmsg_popstr (reply);
            zmsg_destroy (&reply2);
        }
        zmsg_destroy (&msg);

    } // while
    zmsg_destroy (&reply);

}
//  --------------------------------------------------------------------------
//  Create a new fty_sensor_gpio_assets

fty_sensor_gpio_assets_t *
fty_sensor_gpio_assets_new (const char* name)
{
    fty_sensor_gpio_assets_t *self = (fty_sensor_gpio_assets_t *) zmalloc (sizeof (fty_sensor_gpio_assets_t));
    assert (self);
    //  Initialize class properties
    self->mlm         = mlm_client_new();
    self->name        = strdup(name);
    self->test_mode   = false;
    self->template_dir = NULL;
    // Declare our zlist for GPIOs tracking
    // Instanciated here and provided to all actors
    _gpx_list = zlistx_new ();
    assert (_gpx_list);

    // Declare zlist item handlers
    zlistx_set_duplicator (_gpx_list, (czmq_duplicator *) sensor_dup);
    zlistx_set_destructor (_gpx_list, (czmq_destructor *) sensor_free);
    zlistx_set_comparator (_gpx_list, (czmq_comparator *) sensor_cmp);

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
        //  Free class properties
        zlistx_purge (_gpx_list);
        zlistx_destroy (&_gpx_list);
        pthread_mutex_unlock (&gpx_list_mutex);
        zstr_free(&self->name);
        mlm_client_destroy (&self->mlm);
        if (self->template_dir)
            zstr_free(&self->template_dir);

        pthread_mutex_destroy(&gpx_list_mutex);
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
        log_error ("Adress for fty-sensor-gpio-assets actor is NULL");
        return;
    }

    fty_sensor_gpio_assets_t *self = fty_sensor_gpio_assets_new(name);
    assert (self);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (self->mlm), NULL);
    assert (poller);

    zsock_signal (pipe, 0);
    log_info ("%s_assets: Started", self->name);

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
                log_debug ("received command %s", cmd);
                if (streq (cmd, "$TERM")) {
                    zstr_free (&cmd);
                    zmsg_destroy (&message);
                    goto exit;
                }
                else if (streq (cmd, "CONNECT")) {
                    char *endpoint = zmsg_popstr (message);
                     if (!endpoint)
                        log_error ("%s:\tMissing endpoint", self->name);
                    assert (endpoint);
                    int r = mlm_client_connect (self->mlm, endpoint, 5000, self->name);
                    if (r == -1)
                        log_error ("%s:\tConnection to endpoint '%s' failed", self->name, endpoint);
                    log_debug("CONNECT %s/%s", endpoint, self->name);
                    zstr_free (&endpoint);
                }
                else if (streq (cmd, "PRODUCER")) {
                    char *stream = zmsg_popstr (message);
                    assert (stream);
                    mlm_client_set_producer (self->mlm, stream);
                    log_debug ("setting PRODUCER on %s", stream);
                    zstr_free (&stream);
                    request_sensor_assets(self);
                }
                else if (streq (cmd, "CONSUMER")) {
                    char *stream = zmsg_popstr (message);
                    char *pattern = zmsg_popstr (message);
                    assert (stream && pattern);
                    mlm_client_set_consumer (self->mlm, stream, pattern);
                    log_debug ("setting CONSUMER on %s/%s", stream, pattern);
                    zstr_free (&stream);
                    zstr_free (&pattern);
                }
                else if (streq (cmd, "TEST")) {
                    self->test_mode = true;
                    log_debug ("TEST=true");
                }
                else if (streq (cmd, "TEMPLATE_DIR")) {
                    self->template_dir = zmsg_popstr (message);
                    log_debug ("fty_sensor_gpio: Using sensors template directory: %s", self->template_dir);
                }
                else {
                    log_warning ("\tUnknown API command=%s, ignoring", cmd);
                }
                zstr_free (&cmd);
            }
            zmsg_destroy (&message);
        }
        else if (which == mlm_client_msgpipe (self->mlm)) {
            zmsg_t *message = mlm_client_recv (self->mlm);
            if (is_fty_proto (message)) {
                fty_proto_t *fmessage = fty_proto_decode (&message);
                if (fty_proto_id (fmessage) == FTY_PROTO_ASSET) {
                    if (fty_proto_aux_string (fmessage, FTY_PROTO_ASSET_AUX_SUBTYPE, "sensorgpio") ||
                        fty_proto_aux_string (fmessage, FTY_PROTO_ASSET_AUX_SUBTYPE, "gpo"))
                        fty_sensor_gpio_handle_asset (self, fmessage);
                }
                fty_proto_destroy (&fmessage);
            }
            zmsg_destroy (&message);
        }
    }
exit:
    zpoller_destroy (&poller);
    fty_sensor_gpio_assets_destroy(&self);
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
fty_sensor_gpio_assets_test (bool verbose)
{
    printf (" * fty_sensor_gpio_assets: ");

    //  @selftest

    // Note: If your selftest reads SCMed fixture data, please keep it in
    // src/selftest-ro; if your test creates filesystem objects, please
    // do so under src/selftest-rw. They are defined below along with a
    // usecase for the variables (assert) to make compilers happy.
    //const char *SELFTEST_DIR_RO = "src/selftest-ro";
    // Note: here, we use the templates from src/data to check if assets
    // are GPIOs
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

    // Common pattern for plain C:
    // char *test_state_file = zsys_sprintf ("%s/state_file", SELFTEST_DIR_RW);
    // assert (test_state_file != NULL);
    // zstr_sendx (fs, "STATE_FILE", test_state_file, NULL);
    // zstr_free (&test_state_file);

    char *test_data_dir = zsys_sprintf ("%s/data/", SELFTEST_DIR_RO);
    assert (test_data_dir != NULL);

    static const char* endpoint = "inproc://fty_sensor_gpio_assets_test";

    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    zstr_sendx (server, "BIND", endpoint, NULL);

    zactor_t *assets = zactor_new (fty_sensor_gpio_assets, (void*)"gpio-assets");
    zstr_sendx (assets, "TEMPLATE_DIR", test_data_dir, NULL);

    zstr_sendx (assets, "TEST", NULL);
    zstr_sendx (assets, "CONNECT", endpoint, NULL);
    zstr_sendx (assets, "CONSUMER", FTY_PROTO_STREAM_ASSETS, ".*", NULL);
    // Use source-provided templates
    zclock_sleep (1000);

    mlm_client_t *asset_generator = mlm_client_new ();
    mlm_client_connect (asset_generator, endpoint, 1000, "fty_sensor_gpio_assets_generator");
    mlm_client_set_producer (asset_generator, FTY_PROTO_STREAM_ASSETS);

    // Test #1: inject a basic list of assets and check it
    {
        log_debug ("fty-sensor-gpio-assets-test: Test #1");
        // Asset 1: DCS001
        zhash_t* aux = zhash_new ();
        zhash_t *ext = zhash_new ();
        zhash_autofree (aux);
        zhash_autofree (ext);
        zhash_update (aux, "type", (void *) "device");
        zhash_update (aux, "subtype", (void *) "sensorgpio");
        zhash_update (aux, "status", (void *) "active");
        zhash_update (aux, "parent_name.1", (void *) "rackcontroller-1");
        zhash_update (ext, "name", (void *) "GPIO-Sensor-Door1");
        zhash_update (ext, "port", (void *) "1");
        zhash_update (ext, "model", (void *) "DCS001");
        zhash_update (ext, "logical_asset", (void *) "Rack1");

        zmsg_t *msg = fty_proto_encode_asset (
                aux,
                "sensorgpio-10",
                FTY_PROTO_ASSET_OP_CREATE,
                ext);

        int rv = mlm_client_send (asset_generator, "device.sensorgpio@sensorgpio-10", &msg);
        assert (rv == 0);
        zhash_destroy (&aux);
        zhash_destroy (&ext);
        zclock_sleep (1000);
        zmsg_destroy (&msg);

        // Asset 2: WLD012
        aux = zhash_new ();
        ext = zhash_new ();
        zhash_update (aux, "type", (void *) "device");
        zhash_update (aux, "subtype", (void *) "sensorgpio");
        zhash_update (aux, "status", (void *) "active");
        zhash_update (aux, "parent_name.1", (void *) "rackcontroller-1");
        zhash_update (ext, "name", (void *) "GPIO-Sensor-Waterleak1");
        zhash_update (ext, "port", (void *) "2");
        zhash_update (ext, "model", (void *) "WLD012");
        zhash_update (ext, "logical_asset", (void *) "Room1");

        msg = fty_proto_encode_asset (
                aux,
                "sensorgpio-11",
                FTY_PROTO_ASSET_OP_CREATE,
                ext);

        rv = mlm_client_send (asset_generator, "device.sensorgpio@sensorgpio-11", &msg);
        assert (rv == 0);
        zhash_destroy (&aux);
        zhash_destroy (&ext);
        zclock_sleep (1000);
        zmsg_destroy (&msg);

        // Asset 3: GPO-Beacon
        aux = zhash_new ();
        ext = zhash_new ();
        zhash_update (aux, "type", (void *) "device");
        zhash_update (aux, "subtype", (void *) "gpo");
        zhash_update (aux, "status", (void *) "active");
        zhash_update (aux, "parent_name.1", (void *) "rackcontroller-1");
        zhash_update (ext, "name", (void *) "GPO-Beacon");
        zhash_update (ext, "port", (void *) "2");

        msg = fty_proto_encode_asset (
                aux,
                "gpo-12",
                FTY_PROTO_ASSET_OP_CREATE,
                ext);

        rv = mlm_client_send (asset_generator, "device.gpo@gpo-12", &msg);
        assert (rv == 0);
        zhash_destroy (&aux);
        zhash_destroy (&ext);
        zclock_sleep (1000);
        zmsg_destroy (&msg);

        // Asset 4: inactive GPO-Beacon
        aux = zhash_new ();
        ext = zhash_new ();
        zhash_update (aux, "type", (void *) "device");
        zhash_update (aux, "subtype", (void *) "gpo");
        zhash_update (aux, "status", (void *) "nonactive");
        zhash_update (aux, "parent_name.1", (void *) "rackcontroller-1");
        zhash_update (ext, "name", (void *) "GPO-Beacon");
        zhash_update (ext, "port", (void *) "3");

        msg = fty_proto_encode_asset (
                aux,
                "gpo-13",
                FTY_PROTO_ASSET_OP_CREATE,
                ext);

        rv = mlm_client_send (asset_generator, "device.gpo@gpo-13", &msg);
        assert (rv == 0);
        zhash_destroy (&aux);
        zhash_destroy (&ext);
        zclock_sleep (1000);
        zmsg_destroy (&msg);

        // Check the result list
        pthread_mutex_lock (&gpx_list_mutex);
        zlistx_t *test_gpx_list = get_gpx_list();
        assert (test_gpx_list);
        int sensors_count = zlistx_size (test_gpx_list);
        assert (sensors_count == 3);
        // Test the first sensor
        _gpx_info_t *gpx_info = (_gpx_info_t *)zlistx_first (test_gpx_list);
        assert (gpx_info);
        assert (streq (gpx_info->asset_name, "sensorgpio-10"));
        assert (streq (gpx_info->ext_name, "GPIO-Sensor-Door1"));
        assert (streq (gpx_info->part_number, "DCS001"));
        assert (gpx_info->gpx_number == 1);
        assert (streq (gpx_info->parent, "rackcontroller-1"));
        assert (streq (gpx_info->location, "Rack1"));
        // Acquired through the template file
        assert (streq (gpx_info->manufacturer, "Eaton"));
        assert (streq (gpx_info->type, "door-contact-sensor"));
        assert (gpx_info->normal_state == GPIO_STATE_CLOSED);
        assert (gpx_info->gpx_direction == GPIO_DIRECTION_IN);
        assert (streq (gpx_info->alarm_severity, "WARNING"));
        assert (streq (gpx_info->alarm_message, "Door has been $status"));

        // Test the 2nd sensor
        gpx_info = (_gpx_info_t *)zlistx_next (test_gpx_list);
        assert (gpx_info);
        assert (streq (gpx_info->asset_name, "sensorgpio-11"));
        assert (streq (gpx_info->ext_name, "GPIO-Sensor-Waterleak1"));
        assert (streq (gpx_info->part_number, "WLD012"));
        assert (gpx_info->gpx_number == 2);
        assert (streq (gpx_info->parent, "rackcontroller-1"));
        assert (streq (gpx_info->location, "Room1"));
        // Acquired through the template file
        assert (streq (gpx_info->manufacturer, "Eaton"));
        assert (streq (gpx_info->type, "water-leak-detector"));
        assert (gpx_info->normal_state == GPIO_STATE_OPENED);
        assert (gpx_info->gpx_direction == GPIO_DIRECTION_IN);

        // Test the GPO
        gpx_info = (_gpx_info_t *)zlistx_next (test_gpx_list);
        assert (gpx_info);
        assert (streq (gpx_info->asset_name, "gpo-12"));
        assert (streq (gpx_info->ext_name, "GPO-Beacon"));
        assert (gpx_info->gpx_number == 2);
        assert (streq (gpx_info->parent, "rackcontroller-1"));
        assert (gpx_info->normal_state == GPIO_STATE_CLOSED);
        assert (gpx_info->gpx_direction == GPIO_DIRECTION_OUT);

        pthread_mutex_unlock (&gpx_list_mutex);
    }

    // Test #2: Using the list of assets from #1, delete asset 3 and check the list
    {
        log_debug ("fty-sensor-gpio-assets-test: Test #2");
        // Asset 1: DCS001
        zhash_t* aux = zhash_new ();
        zhash_t *ext = zhash_new ();
        zhash_autofree (aux);
        zhash_autofree (ext);
        zhash_update (aux, "type", (void *) "device");
        zhash_update (aux, "subtype", (void *) "gpo");

        zmsg_t *msg = fty_proto_encode_asset (
                aux,
                "gpo-12",
                FTY_PROTO_ASSET_OP_DELETE,
                ext);

        int rv = mlm_client_send (asset_generator, "device.gpo@gpo-12", &msg);
        assert (rv == 0);
        zhash_destroy (&aux);
        zhash_destroy (&ext);
        zclock_sleep (1000);
        zmsg_destroy (&msg);

        // Check the result list
        pthread_mutex_lock (&gpx_list_mutex);
        zlistx_t *test_gpx_list = get_gpx_list();
        assert (test_gpx_list);
        int sensors_count = zlistx_size (test_gpx_list);
        assert (sensors_count == 2);
        log_debug("test_gpx_list = %i", sensors_count);

        pthread_mutex_unlock (&gpx_list_mutex);
    }
    // Test #3: Using the list of assets from #1, update asset 1 with overriden
    // 'normal-state' and check the list
    {
        log_debug ("fty-sensor-gpio-assets-test: Test #3");
        // Asset 1: DCS001
        zhash_t* aux = zhash_new ();
        zhash_t *ext = zhash_new ();
        zhash_autofree (aux);
        zhash_autofree (ext);
        zhash_update (aux, "type", (void *) "device");
        zhash_update (aux, "subtype", (void *) "sensorgpio");
        zhash_update (aux, "status", (void *) "active");
        zhash_update (aux, "parent_name.1", (void *) "rackcontroller-1");
        zhash_update (ext, "name", (void *) "GPIO-Sensor-Door1");
        zhash_update (ext, "normal_state", (void *) "opened");
        zhash_update (ext, "port", (void *) "1");
        zhash_update (ext, "model", (void *) "DCS001");
        zhash_update (ext, "logical_asset", (void *) "Rack2");

        zmsg_t *msg = fty_proto_encode_asset (
                aux,
                "sensorgpio-10",
                FTY_PROTO_ASSET_OP_UPDATE,
                ext);

        int rv = mlm_client_send (asset_generator, "device.sensorgpio@sensorgpio-10", &msg);
        assert (rv == 0);
        zhash_destroy (&aux);
        zhash_destroy (&ext);
        zclock_sleep (1000);
        zmsg_destroy (&msg);

        // Check the result list
        pthread_mutex_lock (&gpx_list_mutex);
        zlistx_t *test_gpx_list = get_gpx_list();
        assert (test_gpx_list);
        int sensors_count = zlistx_size (test_gpx_list);
        assert (sensors_count == 2);
        // Only test the first sensor
        _gpx_info_t *gpx_info = (_gpx_info_t *)zlistx_first (test_gpx_list);
        assert (gpx_info);
        gpx_info = (_gpx_info_t *)zlistx_next (test_gpx_list);
        assert (gpx_info);
        assert (streq (gpx_info->asset_name, "sensorgpio-10"));
        assert (streq (gpx_info->ext_name, "GPIO-Sensor-Door1"));
        assert (streq (gpx_info->part_number, "DCS001"));
        assert (gpx_info->gpx_number == 1);
        assert (streq (gpx_info->parent, "rackcontroller-1"));
        assert (streq (gpx_info->location, "Rack2"));
        // Main point: normal_state is now "opened"!
        assert (gpx_info->normal_state == GPIO_STATE_OPENED);
        // Other data are unchanged
        assert (streq (gpx_info->manufacturer, "Eaton"));
        assert (streq (gpx_info->type, "door-contact-sensor"));
        assert (gpx_info->gpx_direction == GPIO_DIRECTION_IN);
        assert (streq (gpx_info->alarm_severity, "WARNING"));
        assert (streq (gpx_info->alarm_message, "Door has been $status"));

        pthread_mutex_unlock (&gpx_list_mutex);
    }

    // Test #4: Using the list of assets from #1, delete asset 1 and check the list
    {
        log_debug ("fty-sensor-gpio-assets-test: Test #4");
        // Asset 1: DCS001
        zhash_t* aux = zhash_new ();
        zhash_t *ext = zhash_new ();
        zhash_autofree (aux);
        zhash_autofree (ext);
        zhash_update (aux, "type", (void *) "device");
        zhash_update (aux, "subtype", (void *) "sensorgpio");

        zmsg_t *msg = fty_proto_encode_asset (
                aux,
                "sensorgpio-10",
                FTY_PROTO_ASSET_OP_DELETE,
                ext);

        int rv = mlm_client_send (asset_generator, "device.sensorgpio@sensorgpio-10", &msg);
        assert (rv == 0);
        zhash_destroy (&aux);
        zhash_destroy (&ext);
        zclock_sleep (1000);
        zmsg_destroy (&msg);

        // Check the result list
        pthread_mutex_lock (&gpx_list_mutex);
        zlistx_t *test_gpx_list = get_gpx_list();
        assert (test_gpx_list);
        int sensors_count = zlistx_size (test_gpx_list);
        assert (sensors_count == 1);
        log_debug("test_gpx_list = %i", sensors_count);
        // There must remain only 'sensorgpio-11'
        _gpx_info_t *gpx_info = (_gpx_info_t *)zlistx_first (test_gpx_list);
        assert (gpx_info);
        assert (streq (gpx_info->asset_name, "sensorgpio-11"));

        pthread_mutex_unlock (&gpx_list_mutex);
    }

    // Test #5: Using the list of assets from #1, update asset 2 with
    // 'status=nonactive' and check the list
    {
        log_debug ("fty-sensor-gpio-assets-test: Test #5");
        // Asset 1: DCS001
        zhash_t* aux = zhash_new ();
        zhash_t *ext = zhash_new ();
        zhash_autofree (aux);
        zhash_autofree (ext);
        zhash_update (aux, "type", (void *) "device");
        zhash_update (aux, "subtype", (void *) "sensorgpio");
        zhash_update (aux, "status", (void *) "nonactive");
        zhash_update (aux, "parent_name.1", (void *) "rackcontroller-1");
        zhash_update (ext, "name", (void *) "GPIO-Sensor-Waterleak1");
        zhash_update (ext, "port", (void *) "2");
        zhash_update (ext, "model", (void *) "WLD012");
        zhash_update (ext, "logical_asset", (void *) "Room1");

        zmsg_t *msg = fty_proto_encode_asset (
                aux,
                "sensorgpio-11",
                FTY_PROTO_ASSET_OP_UPDATE,
                ext);

        int rv = mlm_client_send (asset_generator, "device.sensorgpio@sensorgpio-11", &msg);
        assert (rv == 0);
        zhash_destroy (&aux);
        zhash_destroy (&ext);
        zclock_sleep (1000);
        zmsg_destroy (&msg);

        // Check the result list
        pthread_mutex_lock (&gpx_list_mutex);
        zlistx_t *test_gpx_list = get_gpx_list();
        assert (test_gpx_list);
        int sensors_count = zlistx_size (test_gpx_list);
        log_debug("test_gpx_list = %i", sensors_count);
        assert (sensors_count == 0);

        pthread_mutex_unlock (&gpx_list_mutex);
    }

    //  @end
    zstr_free (&test_data_dir);
    mlm_client_destroy (&asset_generator);
    zactor_destroy (&assets);
    zactor_destroy (&server);
    printf ("OK\n");
}
