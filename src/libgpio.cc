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
    bool verbose;            // is actor verbose or not
    int  gpo_offset;         // offset to access GPO pins
    int  gpi_offset;         // offset to access GPI pins
    int  gpo_count;          // number of supported GPO
    int  gpi_count;          // number of supported GPI
};

//  Private functions forward declarations

static int libgpio_export(libgpio_t *self, int pin);
static int libgpio_unexport(libgpio_t *self, int pin);
static int libgpio_set_direction(libgpio_t *self, int pin, int dir);


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
    self->verbose = false;

    return self;
}

//  --------------------------------------------------------------------------
//  Set the target address of the GPIO chipset

void
libgpio_set_gpio_base_address (libgpio_t *self, int GPx_base_index)
{
    zsys_debug ("%s: setting address to %i", __func__, GPx_base_index);
    self->gpio_base_address = GPx_base_index;
}

//  --------------------------------------------------------------------------
//  Set the offset to access GPO pins

void
libgpio_set_gpo_offset (libgpio_t *self, int gpo_offset)
{
    zsys_debug ("%s: setting GPO offset to %i", __func__, gpo_offset);
    self->gpo_offset = gpo_offset;
}

//  --------------------------------------------------------------------------
//  Set the offset to access GPI pins

void
libgpio_set_gpi_offset (libgpio_t *self, int gpi_offset)
{
    zsys_debug ("%s: setting GPI offset to %i", __func__, gpi_offset);
    self->gpi_offset = gpi_offset;
}

//  --------------------------------------------------------------------------
//  Set the number of supported GPI
void
libgpio_set_gpi_count (libgpio_t *self, int gpi_count)
{
    zsys_debug ("%s: setting GPI count to %i", __func__, gpi_count);
    self->gpi_count = gpi_count;
}

//  --------------------------------------------------------------------------
//  Set the number of supported GPO
void
libgpio_set_gpo_count (libgpio_t *self, int gpo_count)
{
    zsys_debug ("%s: setting GPO count to %i", __func__, gpo_count);
    self->gpo_count = gpo_count;
}


//  --------------------------------------------------------------------------
//  Set the test mode

void
libgpio_set_test_mode (libgpio_t *self, bool test_mode)
{
    zsys_debug ("%s: setting test_mode to '%s'", __func__, (test_mode == true)?"True":"False");
    self->test_mode = test_mode;
}

//  --------------------------------------------------------------------------
//  Set the verbosity

void
libgpio_set_verbose (libgpio_t *self, bool verbose)
{
    zsys_debug ("%s: setting verbose to '%s'", __func__, (verbose == true)?"True":"False");
    self->verbose = verbose;
}

//  --------------------------------------------------------------------------
//  Read a GPI or GPO status
int
libgpio_read (libgpio_t *self, int GPx_number, int direction)
{
    char path[GPIO_VALUE_MAX];
    char value_str[3];
    int fd;

    // Use the value provided in "direction"
    if (self->test_mode)
        return direction;

    // Sanity check
    if (GPx_number > self->gpi_count) {
        zsys_error("Requested GPx is higher than the count of supported GPIO!");
        return -1;
    }

    int pin = (GPx_number + self->gpi_offset);

    my_zsys_debug (self->verbose, "%s: reading GPx #%i (pin %i)", GPx_number, pin);

    // Enable the desired GPIO
    if (libgpio_export(self, pin) == -1)
        return -1;

    // Set its direction
    if (libgpio_set_direction(self, pin, direction) == -1)
        return -1;

    snprintf(path, GPIO_VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin + self->gpio_base_address);
    fd = open(path, O_RDONLY);
    if (fd == -1) {
        zsys_error("Failed to open gpio value for reading!");
        return -1;
    }

    if (read(fd, value_str, 3) == -1) {
        zsys_error("Failed to read value!");
        close(fd);
        return -1;
    }

    close(fd);

    if (libgpio_unexport(self, pin) == -1) {
        return -1;
    }

    return(atoi(value_str));
}
//  --------------------------------------------------------------------------
//  Write a GPO (to enable or disable it)
int
libgpio_write (libgpio_t *self, int GPO_number, int value)
{
    static const char s_values_str[] = "01";
    char path[GPIO_VALUE_MAX];
    int fd;
    int retval = 0;

    // Simply return "ok" in test mode
    if (self->test_mode)
        return retval;

    // Sanity check
    if (GPO_number > self->gpo_count) {
        zsys_error("Requested GPx is higher than the count of supported GPIO!");
        return -1;
    }

    int pin = (GPO_number + self->gpo_offset);

    my_zsys_debug (self->verbose, "%s: writing GPO #%i (pin %i)", GPO_number, pin);

    snprintf(path, GPIO_VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin + self->gpio_base_address);
    fd = open(path, O_WRONLY);
    if (fd == -1) {
        zsys_error("Failed to open gpio value for writing!");
        return(-1);
    }

    if (write(fd, &s_values_str[GPIO_STATE_CLOSED == value ? 0 : 1], 1) != 1) {
        zsys_error("Failed to write value!");
        retval = -1;
    }

    my_zsys_debug (self->verbose, "%s: result %i", retval);

    close(fd);
    return retval;
}


//  --------------------------------------------------------------------------
//  Get the textual name for a status
string
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

    if ( (streq (status_name, "closed")) || (streq (status_name, "close")) ) {
        status_value = GPIO_STATE_CLOSED;
    }
    else if ( (streq (status_name, "opened")) || (streq (status_name, "open")) ) {
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

    libgpio_t *self = libgpio_new ();
    assert (self);
    libgpio_destroy (&self);
    //  @end
    printf ("OK\n");
}

//  --------------------------------------------------------------------------
//  Private functions

//  --------------------------------------------------------------------------
//  Set the current GPIO pin to act on

int libgpio_export(libgpio_t *self, int pin)
{
    char buffer[GPIO_BUFFER_MAX];
    ssize_t bytes_written;
    int fd;
    int retval = 0;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd == -1) {
        zsys_error("Failed to open export for writing!");
        return -1;
    }

    bytes_written = snprintf(buffer, GPIO_BUFFER_MAX, "%d", pin + self->gpio_base_address);
    if (write(fd, buffer, bytes_written) < bytes_written) {
        retval = -1;
    }

    close(fd);
    return retval;
}

//  --------------------------------------------------------------------------
//  Unset the current GPIO pin to act on

int libgpio_unexport(libgpio_t *self, int pin)
{
    char buffer[GPIO_BUFFER_MAX];
    ssize_t bytes_written;
    int fd;
    int retval = 0;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd == -1) {
      zsys_error("Failed to open unexport for writing!");
      return -1;
    }

    bytes_written = snprintf(buffer, GPIO_BUFFER_MAX, "%d", pin + self->gpio_base_address);
    if (write(fd, buffer, bytes_written) < bytes_written) {
        retval = -1;
    }

    close(fd);
    return retval;
}


//  --------------------------------------------------------------------------
//  Set the current GPIO direction to 'in' (read) or 'out' (write)

int libgpio_set_direction(libgpio_t *self, int pin, int direction)
{
    static const char s_directions_str[]  = "in\0out";
    int retval = 0;

    char path[GPIO_DIRECTION_MAX];
    int fd;

    snprintf(path, GPIO_DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin + self->gpio_base_address);
    fd = open(path, O_WRONLY);
    if (fd == -1) {
        zsys_error("Failed to open gpio direction for writing!");
        return -1;
    }

    if (write(fd, &s_directions_str[GPIO_DIRECTION_IN == direction ? 0 : 3],
      GPIO_DIRECTION_IN == direction ? 2 : 3) == -1) {
        zsys_error("Failed to set direction!");
        retval = -1;
    }

    close(fd);
    return retval;
}

