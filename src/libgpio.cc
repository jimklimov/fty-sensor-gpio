/*  =========================================================================
    libgpio - General Purpose Input/Output (GPIO) sensors library

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
    libgpio - General Purpose Input/Output (GPIO) sensors library
@discuss
@end
*/

#include "fty_sensor_gpio_classes.h"

//  Structure of our class

struct _libgpio_t {
    int  gpio_base_address;  // Base address of the GPIOs chipset
    bool test_mode;          // true if we are in test mode, false otherwise
    int  gpo_offset;         // offset to access GPO pins
    int  gpi_offset;         // offset to access GPI pins
    int  gpo_count;          // number of supported GPO
    int  gpi_count;          // number of supported GPI
    zhashx_t *gpi_mapping;   // mapping for GPIs
    zhashx_t *gpo_mapping;   // mapping for GPOs
};
// FIXME: libgpio should be shared with -server and -asset too
int  _gpo_count = 0;
int  _gpi_count = 0;

//  Private functions forward declarations

static int libgpio_export(libgpio_t *self, int pin);
static int libgpio_unexport(libgpio_t *self, int pin);
static int libgpio_set_direction(libgpio_t *self, int pin, int dir);
static int mkpath(char* file_path, mode_t mode);
// FIXME: use zsys_dir_create (...);

//  Test mode variables
const char *SELFTEST_DIR_RO = "src/selftest-ro";
const char *SELFTEST_DIR_RW = "src/selftest-rw";

void *dup_int_ptr (const void *ptr)
{
    if (ptr == NULL) {
        log_error ("Attempt to dup NULL");
        return NULL ;
    }
    int *new_ptr = (int *) zmalloc (sizeof (int));
    *new_ptr = *(const int *)ptr;
    return (void *)new_ptr;
}

static void free_fn (void ** self_ptr)
{
    if (!self_ptr || !*self_ptr) {
        log_error ("Attempt to free NULL");
        return;
    }
    free (*self_ptr);
}


//  --------------------------------------------------------------------------
//  Create a new libgpio

libgpio_t *
libgpio_new (void)
{
    libgpio_t *self = (libgpio_t *) zmalloc (sizeof (libgpio_t));
    assert (self);
    //  Initialize class properties here
    self->gpio_base_address = GPIO_BASE_INDEX;
    self->gpo_offset = 0;
    self->gpi_offset = 0;
    self->gpo_count = 0;
    self->gpi_count = 0;
    self->test_mode = false;
    self->gpi_mapping = zhashx_new ();
    zhashx_set_key_duplicator (self->gpi_mapping, dup_int_ptr);
    zhashx_set_duplicator (self->gpi_mapping, dup_int_ptr);
    zhashx_set_destructor (self->gpi_mapping, free_fn);
    assert (self->gpi_mapping);
    self->gpo_mapping = zhashx_new ();
    zhashx_set_key_duplicator (self->gpo_mapping, dup_int_ptr);
    zhashx_set_duplicator (self->gpo_mapping, dup_int_ptr);
    zhashx_set_destructor (self->gpo_mapping, free_fn);
    assert (self->gpo_mapping);

    return self;
}

//  --------------------------------------------------------------------------
//  Set the target address of the GPIO chipset

void
libgpio_set_gpio_base_address (libgpio_t *self, int GPx_base_index)
{
    log_debug ("setting address to %i", GPx_base_index);
    self->gpio_base_address = GPx_base_index;
}

//  --------------------------------------------------------------------------
//  Set the offset to access GPO pins

void
libgpio_set_gpo_offset (libgpio_t *self, int gpo_offset)
{
    log_debug ("setting GPO offset to %i", gpo_offset);
    self->gpo_offset = gpo_offset;
}

//  --------------------------------------------------------------------------
//  Set the offset to access GPI pins

void
libgpio_set_gpi_offset (libgpio_t *self, int gpi_offset)
{
    log_debug ("setting GPI offset to %i", gpi_offset);
    self->gpi_offset = gpi_offset;
}

//  --------------------------------------------------------------------------
//  Set the number of supported GPI
void
libgpio_set_gpi_count (libgpio_t *self, int gpi_count)
{
    log_debug ("setting GPI count to %i", gpi_count);
    self->gpi_count = gpi_count;
    _gpi_count = gpi_count;
}

//  --------------------------------------------------------------------------
//  Get the number of supported GPI
int
libgpio_get_gpi_count ()
{
    return _gpi_count;
}

//  --------------------------------------------------------------------------
//  Set the number of supported GPO
void
libgpio_set_gpo_count (libgpio_t *self, int gpo_count)
{
    log_debug ("setting GPO count to %i", gpo_count);
    self->gpo_count = gpo_count;
    _gpo_count = gpo_count;
}

//  --------------------------------------------------------------------------
//  Get the number of supported GPO
int
libgpio_get_gpo_count ()
{
    return _gpo_count;
}

//---------------------------------------------------------------------------
// Add mapping GPI number -> HW pin number
void
libgpio_add_gpi_mapping (libgpio_t *self, int port_num, int pin_num)
{
    log_debug ("adding GPI mapping from port %d to pin %d", port_num, pin_num);
    zhashx_insert (self->gpi_mapping, (void *)&port_num, (void *)&pin_num);
}

//---------------------------------------------------------------------------
// Add mapping GPO number -> HW pin number
void
libgpio_add_gpo_mapping (libgpio_t *self, int port_num, int pin_num)
{
    log_debug ("adding GPIO mapping from port %d to pin %d", port_num, pin_num);
    zhashx_insert (self->gpo_mapping, (void *)&port_num, (void *)&pin_num);
}
//  --------------------------------------------------------------------------
//  Set the test mode

void
libgpio_set_test_mode (libgpio_t *self, bool test_mode)
{
    log_debug ("setting test_mode to '%s'", (test_mode == true)?"True":"False");
    self->test_mode = test_mode;
}

//  --------------------------------------------------------------------------
//  Compute and store HW pin number
int
libgpio_compute_pin_number (libgpio_t *self, int GPx_number, int direction)
{
    if (direction == GPIO_DIRECTION_IN) {
        int *found_pin_ptr = (int *) zhashx_lookup (self->gpi_mapping, (const void *)&GPx_number);
        if (found_pin_ptr == NULL) {
            int pin = self->gpio_base_address + self->gpi_offset + GPx_number;
            zhashx_update (self->gpi_mapping, (const void *)&GPx_number, (void *)&pin);
            return pin;
        }
        else
            return *found_pin_ptr;
    }
    else {
        int *found_pin_ptr = (int *) zhashx_lookup (self->gpo_mapping, (const void *)&GPx_number);
        if (found_pin_ptr == NULL) {
            int pin = self->gpio_base_address + self->gpo_offset + GPx_number;
            zhashx_update (self->gpo_mapping, (const void *)&GPx_number, (void *)&pin);
            return pin;
        }
        else
            return *found_pin_ptr;
    }
}

//  --------------------------------------------------------------------------
//  Read a GPI or GPO status
int
libgpio_read (libgpio_t *self, int GPx_number, int direction)
{
    char path[GPIO_VALUE_MAX];
    char value_str[3];
    int retvalue = -1;
    int fd;
    int retries = GPIO_MAX_RETRY;

    memset(&value_str[0], 0, 3);

    // Sanity check
    if (GPx_number > ((direction==GPIO_DIRECTION_IN)?self->gpi_count:self->gpo_count)) {
        log_error("Requested GPx is higher than the count of supported GPIO!");
        return -1;
    }

    int pin;
    int *pin_ptr;
    if (direction == GPIO_DIRECTION_IN)
        pin_ptr = (int *)(zhashx_lookup (self->gpi_mapping, (const void *)&GPx_number));
    else
        pin_ptr = (int *)(zhashx_lookup (self->gpo_mapping, (const void *)&GPx_number));
    if (pin_ptr == NULL)
        pin = libgpio_compute_pin_number (self, GPx_number, direction);
    else
        pin = *pin_ptr;
    log_debug ("reading GPx #%i (pin %i)", GPx_number, pin);
    // Enable the desired GPIO
    if (libgpio_export(self, pin) == -1) {
        log_debug ("Failed to export, aborting...");
        goto end;
    }

    // Set its direction, with a possible delay
    while (libgpio_set_direction(self, pin, direction) == -1) {

        log_warning ("Failed to set direction, retrying...");

        // Wait a bit for the sysfs to be created and udev rules to be applied
        // so that we get the right privileges applied
        zclock_sleep(500);

        if (retries-- > 0) {
            continue;
        }

        log_error("Failed to set direction after %i tries. Aborting!", GPIO_MAX_RETRY);
        goto end;
    }

    snprintf(path, GPIO_VALUE_MAX, "%s/sys/class/gpio/gpio%d/value",
        (self->test_mode)?SELFTEST_DIR_RW:"", // trick #1 to allow testing
        pin);
    // trick #2 to allow testing
    if (self->test_mode)
        mkpath(path, 0777);
    fd = open(path, O_RDONLY | ((self->test_mode)?O_CREAT:0), 0777);
    if (fd == -1) {
        log_error("Failed to open gpio '%s' for reading!", path);
        goto end;
    }

    if (read(fd, value_str, 3) <= 0) {
        log_error("Failed to read value!");
        close(fd);
        goto end;
    }
    retvalue = atoi(&value_str[0]);

    log_trace ("read value '%c'", value_str[0]);

    close(fd);

end:
    if (libgpio_unexport(self, pin) == -1) {
        log_error ("Failed to unexport...");
        return -1;
    }

    return retvalue;
}
//  --------------------------------------------------------------------------
//  Write a GPO (to enable or disable it)
int
libgpio_write (libgpio_t *self, int GPO_number, int value)
{
    static const char s_values_str[] = "01";
    char path[GPIO_VALUE_MAX];
    int fd = -1;
    int retval = -1;
    int retries = GPIO_MAX_RETRY;

    // Sanity check
    if (GPO_number > self->gpo_count) {
        log_error("Requested GPx is higher than the count of supported GPIO!");
        return -1;
    }

    int pin;
    int *pin_ptr = (int *)(zhashx_lookup (self->gpo_mapping, (const void *)&GPO_number));
    if (pin_ptr == NULL)
        pin = libgpio_compute_pin_number (self, GPO_number, GPIO_DIRECTION_OUT);
    else
        pin = *pin_ptr;

    log_trace ("writing GPO #%i (pin %i)", GPO_number, pin);

    // Enable the desired GPIO
    if (libgpio_export(self, pin) == -1) {
        log_error ("Failed to export, aborting...");
        goto end;
    }

    // Set its direction, with a possible delay
    while (libgpio_set_direction(self, pin, GPIO_DIRECTION_OUT) == -1) {

        log_warning ("Failed to set direction, retrying...");

        // Wait a bit for the sysfs to be created and udev rules to be applied
        // so that we get the right privileges applied
        zclock_sleep(500);

        if (retries-- > 0) {
            continue;
        }

        log_error("Failed to set direction after %i tries. Aborting!", GPIO_MAX_RETRY);
        goto end;
    }

    // trick #2 to allow testing
    snprintf(path, GPIO_VALUE_MAX, "%s/sys/class/gpio/gpio%d/value",
        (self->test_mode)?SELFTEST_DIR_RW:"", // trick #1 to allow testing
        pin);
    if (self->test_mode)
        mkpath(path, 0777);
    fd = open(path, O_WRONLY | ((self->test_mode)?O_CREAT:0), 0777);
    if (fd == -1) {
        log_error("Failed to open gpio value for writing (path: %s)!", path);
        retval = -1;
        goto end;
    }

    if (write(fd, &s_values_str[GPIO_STATE_CLOSED == value ? 0 : 1], 1) != 1) {
        log_error("Failed to write value!");
        retval = -1;
    }
    else
        retval = 0;

    log_trace ("wrote value '%i' with result %i", value, retval);

    close(fd);
end:
    if (libgpio_unexport(self, pin) == -1) {
        retval = -1;
    }
    return retval;
}


//  --------------------------------------------------------------------------
//  Get the textual name for a status
const string
libgpio_get_status_string (int value)
{
    string status_str;

    switch (value) {
        case GPIO_STATE_CLOSED:
            status_str = "closed";
            break;
        case GPIO_STATE_OPENED:
            status_str = "opened";
            break;
        case GPIO_STATE_UNKNOWN:
        default:
            status_str = ""; // FIXME: return "unknown"?
    }
    return status_str;
}

//  --------------------------------------------------------------------------
//  Get the numeric value for a status name
int
libgpio_get_status_value (const char* status_name)
{
    int status_value = GPIO_STATE_CLOSED;

    if ( (streq (status_name, "closed")) || (streq (status_name, "close"))
        || (streq (status_name, "disabled")) || (streq (status_name, "disable"))
        || (streq (status_name, "low")) ) {
        status_value = GPIO_STATE_CLOSED;
    }
    else if ( (streq (status_name, "opened")) || (streq (status_name, "open"))
             || (streq (status_name, "enabled")) || (streq (status_name, "enable"))
             || (streq (status_name, "high")) ) {
        status_value = GPIO_STATE_OPENED;
    }
    else
        status_value = GPIO_STATE_UNKNOWN;

    return status_value;
}

//  --------------------------------------------------------------------------
//  Destroy the libgpio

void
libgpio_destroy (libgpio_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        libgpio_t *self = *self_p;
        //  Free class properties here
        zhashx_destroy (&self->gpi_mapping);
        zhashx_destroy (&self->gpo_mapping);
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
libgpio_test (bool verbose)
{
    printf (" * libgpio: ");

    // Note: for testing purpose, we use a trick, that is to access the /sys
    // FS under our SELFTEST_DIR_RW.

    //  @selftest
    //  Simple create/destroy test
    libgpio_t *self = libgpio_new ();
    assert (self);
    libgpio_destroy (&self);

    // Setup
    self = libgpio_new ();
    assert (self);
    libgpio_set_test_mode (self, true);
    libgpio_set_gpio_base_address (self, 0);
    // We use the same offset for GPI and GPO, to be able to write a GPO
    // and read the same for GPI
    libgpio_set_gpi_offset (self, 0);
    libgpio_set_gpo_offset (self, 0);
    libgpio_set_gpi_count (self, 10);
    libgpio_set_gpo_count (self, 5);

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

    // Common pattern for plain C:
    // char *test_state_file = zsys_sprintf ("%s/state_file", SELFTEST_DIR_RW);
    // assert (test_state_file != NULL);
    // zstr_sendx (fs, "STATE_FILE", test_state_file, NULL);
    // zstr_free (&test_state_file);

    // Write test
    // Let's first write to the dummy GPIO, so that the read works afterward
    assert( libgpio_write (self, 1, GPIO_STATE_CLOSED) == 0);

    // Read test
    assert( libgpio_read (self, 1, GPIO_DIRECTION_IN) == GPIO_STATE_CLOSED );

    // Value resolution test
    assert( libgpio_get_status_value("opened") == GPIO_STATE_OPENED );
    assert( libgpio_get_status_value("closed") == GPIO_STATE_CLOSED );
    assert( libgpio_get_status_value( libgpio_get_status_string(GPIO_STATE_CLOSED).c_str() ) == GPIO_STATE_CLOSED );

    // Delete all test files
    std::string sys_fn = string(SELFTEST_DIR_RW) + "/sys";
    zdir_t *dir = zdir_new (sys_fn.c_str(), NULL);
    assert (dir);
    zdir_remove (dir, true);
    zdir_destroy (&dir);

    libgpio_destroy (&self);

    //  @end
    printf ("OK\n");
}

//  --------------------------------------------------------------------------
//  Private functions

//  --------------------------------------------------------------------------
//  Set the current GPIO pin to act on

int
libgpio_export(libgpio_t *self, int pin)
{
    char buffer[GPIO_BUFFER_MAX];
    char path[GPIO_VALUE_MAX];
    ssize_t bytes_written;
    int fd;
    int retval = 0;

    snprintf(path, GPIO_VALUE_MAX, "%s/sys/class/gpio/export",
        (self->test_mode)?SELFTEST_DIR_RW:""); // trick #1 to allow testing
    // trick #2 to allow testing
    if (self->test_mode)
        mkpath(path, 0777);
    fd = open(path, O_WRONLY | ((self->test_mode)?O_CREAT:0), 0777);
    if (fd == -1) {
        log_error("Failed to open %s for writing! %i", path, errno);
        return -1;
    }
    log_debug ("exporting pin %d", pin);

    bytes_written = snprintf(buffer, GPIO_BUFFER_MAX, "%d", pin);
    if (write(fd, buffer, bytes_written) < bytes_written) {
        log_error ("wrote less than %i bytes (errno %i)",
            bytes_written, errno);
        retval = -1;
    }

    close(fd);
    return retval;
}

//  --------------------------------------------------------------------------
//  Unset the current GPIO pin to act on

int
libgpio_unexport(libgpio_t *self, int pin)
{
    char buffer[GPIO_BUFFER_MAX];
    char path[GPIO_VALUE_MAX];
    ssize_t bytes_written;
    int fd;
    int retval = 0;

    snprintf(path, GPIO_VALUE_MAX, "%s/sys/class/gpio/unexport",
        (self->test_mode)?SELFTEST_DIR_RW:""); // trick #1 to allow testing
    // trick #2 to allow testing
    if (self->test_mode)
        mkpath(path, 0777);
    fd = open(path, O_WRONLY | ((self->test_mode)?O_CREAT:0), 0777);
    if (fd == -1) {
      log_error("Failed to open unexport for writing!");
      return -1;
    }

    bytes_written = snprintf(buffer, GPIO_BUFFER_MAX, "%d", pin);
    if (write(fd, buffer, bytes_written) < bytes_written) {
        log_error ("wrote less than %i bytes",
            bytes_written);
        retval = -1;
    }

    close(fd);
    return retval;
}


//  --------------------------------------------------------------------------
//  Set the current GPIO direction to 'in' (read) or 'out' (write)

int
libgpio_set_direction(libgpio_t *self, int pin, int direction)
{
    static const char s_directions_str[]  = "in\0out";
    int retval = 0;

    char path[GPIO_DIRECTION_MAX];
    int fd;

    snprintf(path, GPIO_DIRECTION_MAX, "%s/sys/class/gpio/gpio%d/direction",
        ((self->test_mode)?SELFTEST_DIR_RW:""), // trick #1 to allow testing
        pin);
    // trick #2 to allow testing
    if (self->test_mode)
        mkpath(path, 0777);
    fd = open(path, O_WRONLY | ((self->test_mode)?O_CREAT:0), 0777);
    if (fd == -1) {
        log_error ("Failed to open %s for writing!", path);
        return -1;
    }

    if (write(fd, &s_directions_str[GPIO_DIRECTION_IN == direction ? 0 : 3],
      GPIO_DIRECTION_IN == direction ? 2 : 3) == -1) {
        log_error ("Failed to set direction!");
        retval = -1;
    }

    close(fd);
    return retval;
}

//  --------------------------------------------------------------------------
//  Helper function to recursively create directories

// FIXME: replace with zsys_dir_create (const char *pathname, ...);
static int
mkpath(char* file_path, mode_t mode)
{
    assert(file_path && *file_path);
    char* p;
    for (p=strchr(file_path+1, '/'); p; p=strchr(p+1, '/')) {
        *p='\0';
        if (mkdir(file_path, mode)==-1) {
            if (errno!=EEXIST) {
                *p='/';
                return -1;
            }
        }
        *p='/';
    }
    return 0;
}
