# fty-sensor-gpio

fty-sensor-gpio is an agent to manage GPI sensors and GPO devices. It is able
to read status of any kind of GPIO devices or set (enable / disable) any

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

...
config file...
...

./src/fty-sensor-gpio -v -c src/fty-sensor-gpio.cfg

systemctl (start|stop|restart|...) fty-sensor-gpio


## Architecture

### Overview

fty-sensor-gpio is composed of two actors:

* assets actor: requests all assets, and listen to ASSETS stream, to find GPIO
sensors and configure the agent. These information are then used by both the
server and the alerts actors.
* server actor: handles GPI polling and related metrics publication. This actor
also handles mailbox requests, to serve the manifest of supported GPIO devices
or act on GPO devices upon command reception
* alerts actor: publish and manage alerts

### Manifest files

...
manifest files are provided in the 'data/' directory of the source tree.
...

--------------------------------------------------------------------------------
manufacturer   = Eaton
part-number    = DCS001
type           = door-contact-sensor
normal-state   = closed
gpx-direction  = GPI
alarm-severity = WARNING
alarm-message  = Door has been $status
--------------------------------------------------------------------------------

Manifest files are named using the 'part-number' field, with the '.tpl'
(template) file extension.


In case where no manifest file is present but the device is a GPIO sensor...
?FIXME?
Need to be provided with the following extended attributes
  gpx_  type           = door-contact-sensor
  gpx_  direction  = GPI
  gpx_  alarm-severity = WARNING
  gpx_  alarm-message  = Door has been $status
  gpx_  manufacturer   = Eaton
If these are provided, a new manifest file will be created?
!!FIXME: check with PO if OK/desirable or not!!



////////////////////////////////////////////////////////////////////////////////

### Commissioning using CSV
name,type,sub_type,location,status,priority,model,gpx_number,normal_state
GPIO-Sensor-Door1,device,sensor,IPC1,active,P1,DCS001,1,opened
--

## Protocols

### Published metrics

Metrics are published on the '_METRICS_SENSOR' stream.

--------------------------------------------------------------------------------
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
--------------------------------------------------------------------------------

### Mailbox requests

It is possible to request the agent GPIO for:
* the manifest of supported GPIO devices
* acting on GPO devices, to activate or de-activate

### RFC-Alerts-List  -  Alerts list protocol
Connects USER peer to ALERTS-LIST-PROVIDER peer.

The USER peer sends one of the following messages using MAILBOX SEND to
ALERT-LIST-PROVIDER peer:

* LIST/state - request list of alerts of specified 'state'

where
* '/' indicates a multipart string message
* 'state' MUST be one of [ ALL | ACTIVE | ACK-WIP | ACK-IGNORE | ACK-PAUSE | ACK-SILENCE ]
* subject of the message MUST be "rfc-alerts-list".


The ALERT-LIST-PROVIDER peer MUST respond with one of the messages back to USER
peer using MAILBOX SEND.

* LIST/state/alert_1[/alert_2]...[/alert_N]
* ERROR/reason

where
* '/' indicates a multipart frame message
* 'state' is string and value MUST be repeated from request
* 'reason' is string detailing reason for error. If requested 'state' does not
    exist, the ALERT-LIST-PROVIDER peer MUST assign NOT_FOUND string as reason.
* 'alert_X' is an encoded ALERT message (from libbiosproto) representing alert
    of requested state and subject of the message MUST be "rfc-alerts-list".

////////////////////////////////////////////////////////////////////////////////
