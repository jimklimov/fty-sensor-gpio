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
    mlm_client_t *mlm;    // malamute client
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
fty_sensor_gpio_server_new (void)
{
    fty_sensor_gpio_server_t *self = (fty_sensor_gpio_server_t *) zmalloc (sizeof (fty_sensor_gpio_server_t));
    assert (self);
    //  Initialize class properties here
    self->mlm  = mlm_client_new();

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
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Check for the existence of a template file according to the asset part nb
//  If one exists, it's a GPIO sensor

static string
is_asset_gpio_sensor (string sensor_partnumber)
{
    zconfig_t *config_template = NULL;

    if (sensor_partnumber == "")
        return "";

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
        //gpi_list[sensor_num].type = sensor_type;
    }
    else
         std::cout << "FAILED to read sensor type" << std::endl;
    char* sensor_normal_state = s_get (config_template, "normal-state",  NULL);
    if (sensor_normal_state) {
        std::cout << "\tsensor_normal_state: " << sensor_normal_state << std::endl;
        /*if (sensor_normal_state == string("opened"))
            gpi_list[sensor_num].normal_state = GPIO_STATUS_OPENED;
        else if (sensor_normal_state == string("closed"))
            gpi_list[sensor_num].normal_state = GPIO_STATUS_CLOSED;
        */
        // else exception...
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

    const char *operation = fty_proto_operation (ftymsg);
    const char *assetname = fty_proto_name (ftymsg);

    zsys_debug ("%s: '%s' operation on asset '%s'", __func__, operation, assetname);

/*
    zhash_t *ext = fty_proto_ext (ftymsg);
    zlist_t *keys = zhash_keys (ext);
    char *key = (char *)zlist_first (keys);
    while (key) {
        if (strncmp ("group.", key, 6) == 0) {
            // this is group
            if (rule_group_exists (rule, (char *) zhash_lookup (ext, key))) {
                zlist_destroy (&keys);
                return 1;
            }
        }
        key = (char *)zlist_next (keys);
    }
    zlist_destroy (&keys);

    if (rule_model_exists (rule, fty_proto_ext_string (ftymsg, "model", "")))
*/
    const char* sensor_partnumber = fty_proto_ext_string (ftymsg, "model", "");
    string config_template = is_asset_gpio_sensor(sensor_partnumber);

    if (streq (operation, "delete")) {
/*
        if (zhash_lookup (self->assets, assetname)) {
            zhash_delete (self->assets, assetname);
        }
        if (zhash_lookup (self->enames, assetname)) {
            zhash_delete (self->enames, assetname);
        }
        return;
*/
    }
    if (streq (operation, "update")) {
        ;
/*
        zlist_t *functions_for_asset = zlist_new ();
        zlist_autofree (functions_for_asset);

        rule_t *rule = (rule_t *)zhash_first (self->rules);
        while (rule) {
            if (is_rule_for_this_asset (rule, ftymsg)) {
                zlist_append (functions_for_asset, (char *)rule_name (rule));
                zsys_debug ("rule '%s' is valid for '%s'", rule_name (rule), assetname);
            }
            rule = (rule_t *)zhash_next (self->rules);
        }
        if (! zlist_size (functions_for_asset)) {
            zsys_debug ("no rule for %s", assetname);
            zhash_delete (self->assets, assetname);
            zlist_destroy (&functions_for_asset);
            return;
        }
        zhash_update (self->assets, assetname, functions_for_asset);
        zhash_freefn (self->assets, assetname, asset_freefn);
        const char *ename = fty_proto_ext_string (ftymsg, "name", NULL);
        if (ename) {
            zhash_update (self->enames, assetname, (void *)ename);
            zhash_freefn (self->enames, assetname, ename_freefn);
        }
*/
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
    bool verbose = false;
    
    fty_sensor_gpio_server_t *self = fty_sensor_gpio_server_new();
    assert (self);

    mlm_client_t *client = mlm_client_new ();
    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);
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
            if (cmd) {
                if (streq (cmd, "$TERM")) {
                    zstr_free (&cmd);
                    zmsg_destroy (&msg);
                    break;
                }
                else if (streq (cmd, "BIND")) {
                    char *endpoint = zmsg_popstr (msg);
                    char *myname = zmsg_popstr (msg);
                    assert (endpoint && myname);
                    mlm_client_connect (self->mlm, endpoint, 5000, myname);
                    zstr_free (&endpoint);
                    zstr_free (&myname);
                }
                else if (streq (cmd, "PRODUCER")) {
                    char *stream = zmsg_popstr (msg);
                    assert (stream);
                    mlm_client_set_producer (self->mlm, stream);
                    zstr_free (&stream);
                }
                else if (streq (cmd, "CONSUMER")) {
                    char *stream = zmsg_popstr (msg);
                    char *pattern = zmsg_popstr (msg);
                    assert (stream && pattern);
                    mlm_client_set_consumer (self->mlm, stream, pattern);
                    zstr_free (&stream);
                    zstr_free (&pattern);
                }
                else if (streq (cmd, "LOADRULES")) {
                    zstr_free (&ruledir);
                    ruledir = zmsg_popstr (msg);
                    assert (ruledir);
                    flexible_alert_load_rules (self, ruledir);
                }
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
#if 0
         else
             if (which == mlm_client_msgpipe (client)) {
                 //TODO: implement actor interface
                 zmsg_t *message = mlm_client_recv (client);
                 if (!message)
                    continue;

                 char *command = zmsg_popstr (message);
                 if (!command) {
                    zmsg_destroy (&message);
                    zsys_warning ("Empty command.");
                    continue;
                 }
zsys_info ("Received command %s", command);
/*
                 if (!streq(command, "INFO") && !streq(command, "INFO-TEST")) {
                    zsys_warning ("fty-sensor-gpio: Received unexpected command '%s'", command);
                    zmsg_t *reply = zmsg_new ();
                    zmsg_addstr(reply, "ERROR");
                    mlm_client_sendto (client, mlm_client_sender (client), "info", NULL, 1000, &reply);
                    zstr_free (&command);
                    zmsg_destroy (&message);
                    continue;
                     
                 } else {
                    zmsg_t *reply = zmsg_new ();
                    char *zuuid = zmsg_popstr (message);
                    fty_info_t *self;
                    if (streq(command, "INFO")) {
                        self = fty_info_new ();
                    }
                    if (streq(command, "INFO-TEST")) {
                        self = fty_info_test_new ();
 
                    }
                    //prepare replied msg content
                    zmsg_addstrf (reply, "%s", zuuid);
                    zhash_insert(self->infos, INFO_UUID, self->uuid);
                    zhash_insert(self->infos, INFO_HOSTNAME, self->hostname);
                    zhash_insert(self->infos, INFO_NAME, self->name);
                    zhash_insert(self->infos, INFO_PRODUCT_NAME, self->product_name);
                    zhash_insert(self->infos, INFO_LOCATION, self->location);
                    zhash_insert(self->infos, INFO_VERSION, self->version);
                    zhash_insert(self->infos, INFO_REST_ROOT, self->rest_root);
                    zhash_insert(self->infos, INFO_REST_PORT, self->rest_port);
                    zframe_t * frame_infos = zhash_pack(self->infos);
                    zmsg_append (reply, &frame_infos);
               
                    mlm_client_sendto (client, mlm_client_sender (client), "info", NULL, 1000, &reply);
                    zstr_free (&zuuid);
                    fty_info_destroy (&self);
                }
*/
                zstr_free (&command);
                zmsg_destroy (&message);
//             }
#endif //#if 0
        }
    }
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
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

    fty_sensor_gpio_server_t *self = fty_sensor_gpio_server_new ();
    assert (self);
    fty_sensor_gpio_server_destroy (&self);
    //  @end
    printf ("OK\n");
}
