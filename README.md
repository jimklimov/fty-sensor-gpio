# fty-sensor-gpio

fty-sensor-gpio is an agent to manage GPI sensors and GPO devices. It is able
to read status of any kind of GPIO devices or set (enable / disable) any GPO.

GPIO devices are basic dry-contact, which are either 'opened' or 'closed', to
indicate a status or enable / disable a device.

## How to build

To build fty-sensor-gpio project run:

```bash
./autogen.sh
./configure
make
make check # to run self-test
```

## How to run

To run fty-sensor-gpio project:

* from within the source tree, run:

```bash
./src/fty-sensor-gpio
```

For the other options available, refer to the manual page of fty-sensor-gpio

* from an installed base, using systemd, run:

```bash
systemctl start fty-sensor-gpio
```

### Configuration file

To configure fty-sensor-gpio, a configuration file exists: fty-sensor-gpio.cfg

Beside from the standard configuration directives, under the server and malamute
sections, the following are also available to adapt to the specific hardware,
under the 'hardware' section:

* gpio_base_address: the target address of the GPIO chipset (488 for the
gpiochip488 on IPC3000)
* gpi_count: the number of GPI (10 on IPC3000)
* gpo_count: the number of GPO (5 on IPC3000)
* gpo_offset: the offset to apply to access GPO pins, from base chipset address.
15 on IPC3000, so GPO pins have +15 offset, i.e. GPO 1 is pin 16, ...
* gpi_offset: the offset to apply to access GPI pins, from base chipset address.
-1 on IPC3000, so GPI pins have -1 offset, i.e. GPI 1 is pin 0, ...

### Commissioning using CSV file

It is possible to declare GPIO sensors through the CSV file.
The following fields and extended attributes are available:

* name (mandatory)
* type (mandatory): set to 'device'
* sub_type (mandatory): set to 'sensor'
* location (optional)
* status (optional)
* priority (optional)
* model (mandatory): is the part number of the GPIO sensor, use for naming
the template files
* gpx_number (mandatory): is the GPI or GPO number to which the sensor is
connected to. The pin number of the GPIO is then computed based on this number
and the configured 'gpi_offset' and 'gpo_offset'.
* normal_state (optional): is the status of the sensor under nominal conditions.
Possible values are 'opened' or 'closed'. When not provided, the value from the
template file is used. Otherwise, the provided value takes precedence.

Example of entries:

```bash
name,type,sub_type,location,status,priority,model,gpx_number,normal_state
GPIO-Sensor-Door1,device,sensor,IPC1,active,P1,DCS001,1,opened
GPIO-GPOTest1,device,sensor,IPC1,active,P1,GPOTEST,1,
```

## Architecture

### Overview

fty-sensor-gpio is composed of 3 actors:

* assets actor: requests all assets, and listen to ASSETS stream, to find GPIO
sensors and configure the agent. These information are then used by both the
server and the alerts actors.
* server actor: handles GPI polling and related metrics publication. This actor
also handles mailbox requests, to serve the manifest of supported GPIO devices,
create additional template files or act on GPO devices upon command reception
* alerts actor: publish alerts

### Template files

Template files describe a GPIO sensor, and provide default values (such as
'normal-state') in case the user does not provide it through the commissioning.

Warning: Template files are mandatory! If there is no template file for a
commissioned sensor, this sensor will not be monitored!

Template files are provided in the 'data/' directory of the source tree, and
installed by default to '/usr/share/fty-sensor-gpio/data/'.

Template files are named using the 'part-number' field, with the '.tpl'
(template) file extension, and have the following format:

```bash
manufacturer   = <value>
part-number    = <value>
type           = <value>
normal-state   = <value>
gpx-direction  = <value>
alarm-severity = <value>
alarm-message  = <value>
```

For example:

```bash
manufacturer   = Eaton
part-number    = DCS001
type           = door-contact-sensor
normal-state   = closed
gpx-direction  = GPI
alarm-severity = WARNING
alarm-message  = Door has been $status
```

'alarm-message' can be adapted at runtime through the use of some variables, to
adapt the alert message:

* "$status": is replaced by the pattern "opened" or "closed"
* "$device_name": is replaced by the sensor name which has generated the alert
* "$location": is replaced by the sensor location


## Protocols

### Published metrics

Metrics are published on the '_METRICS_SENSOR' stream.

For example:

```bash
stream=_METRICS_SENSOR
sender=fty-sensor-gpio
subject=status.GPI2@sensor-90
D: 13-01-28 10:22:53 FTY_PROTO_METRIC:
D: 13-01-28 10:22:53     aux=
D: 13-01-28 10:22:53         port=GPI2
D: 13-01-28 10:22:53     time=1359368573
D: 13-01-28 10:22:53     ttl=300
D: 13-01-28 10:22:53     type='status.GPI2'
D: 13-01-28 10:22:53     name='sensor-90'
D: 13-01-28 10:22:53     value='opened'
D: 13-01-28 10:22:53     unit=''
```

### Published alerts

Alerts are published on the '_ALERTS_SYS' stream.

The content of the alarm message is created using:

* the value of the 'alarm-message' field from the template file,
* the user provided value at commissioning time.

This may be completed at runtime, if the message contains some variables.


### Mailbox requests

It is possible to request the agent GPIO for:

* getting the manifest of one, several or all supported GPIO devices, in simple or detailed format,
* creating a new template file, to add support for a new GPIO sensor,
* acting on GPO devices, to activate or de-activate.

#### Action on GPO sensors

The USER peer sends the following messages using MAILBOX SEND to
FTY-SENSOR-GPIO-AGENT ("fty-sensor-gpio") peer:

* GPO_INTERACTION/sensor/action - apply 'action' (open | close) on 'sensor' (asset or ext name)

where
* '/' indicates a multipart string message
* 'sensor' MUST be the sensor asset name or ext name
* 'action' MUST be one of [ open | opened | high | close | closed | low ]
* subject of the message MUST be "GPO_INTERACTION".

The FTY-SENSOR-GPIO-AGENT peer MUST respond with one of the messages back to USER
peer using MAILBOX SEND.

* OK
* ERROR/reason

where
* '/' indicates a multipart frame message
* 'reason' is string detailing reason for error. Possible values are:
ASSET_NOT_FOUND / SET_VALUE_FAILED / UNKNOWN_VALUE / BAD_COMMAND.

#### Detailed manifest of supported sensors

The USER peer sends the following messages using MAILBOX SEND to
FTY-SENSOR-GPIO-AGENT ("fty-sensor-gpio") peer:

* GPIO_MANIFEST/<sensor 1 part number>/.../<sensor N part number> - get information on sensor(s)

where
* '/' indicates a multipart string message
* 'sensor x part number' is the part number of the sensor(s), to get information
on. When empty, the agent returns information on all supported sensors
* subject of the message MUST be "GPIO_MANIFEST".

The FTY-SENSOR-GPIO-AGENT peer MUST respond with one of the messages back to USER
peer using MAILBOX SEND.

* OK/<sensor 1 description>/.../<sensor N description> = non-empty
* ERROR/<reason>

where
* '/' indicates a multipart frame message
* 'reason' is string detailing reason for error. Possible values are:
ASSET_NOT_FOUND / BAD_COMMAND
* 'sensor x description' is a string with details on the sensor with the format:
sensor_partnumber/manufacturer/type/normal_state/gpx_direction/alarm_severity/alarm_message

#### Summary manifest of supported sensors

The USER peer sends the following messages using MAILBOX SEND to
FTY-SENSOR-GPIO-AGENT ("fty-sensor-gpio") peer:

* GPIO_MANIFEST_SUMMARY - get the list of supported sensors

where
* subject of the message MUST be "GPIO_MANIFEST_SUMMARY".

The FTY-SENSOR-GPIO-AGENT peer MUST respond with one of the messages back to USER
peer using MAILBOX SEND.

* OK/<sensor 1 description>/.../<sensor N description> = non-empty
* ERROR/<reason>

where
* '/' indicates a multipart frame message
* 'reason' is string detailing reason for error. Possible values are:
ASSET_NOT_FOUND / BAD_COMMAND
* 'sensor x description' is a string with details on the sensor with the format:
sensor_partnumber/manufacturer

#### Create a new sensor template file

The USER peer sends the following messages using MAILBOX SEND to
FTY-SENSOR-GPIO-AGENT ("fty-sensor-gpio") peer:

* GPIO_TEMPLATE_ADD/<sensor description> - request the creation of a sensor template file

where
* 'sensor description' is a string with details on the sensor with the format:
sensor_partnumber/manufacturer/type/normal_state/gpx_direction/alarm_severity/alarm_message
* subject of the message MUST be "GPIO_TEMPLATE_ADD".

The FTY-SENSOR-GPIO-AGENT peer MUST respond with one of the messages back to USER
peer using MAILBOX SEND.

* OK
* ERROR/<reason>

where
* '/' indicates a multipart frame message
* 'reason' is string detailing reason for error. Possible values are:
...
