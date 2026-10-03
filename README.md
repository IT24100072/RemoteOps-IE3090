# RemoteOps - IE3090 Network Programming

## Student Information

- Registration Number: IT24100072
- Numeric ID: 24100072
- TCP Port: 9410
- Session ID: SID:2700
- Authentication Token: OPS-0072
- Storage Directory: /home/tasa/remoteops/agentfiles/IT24100072
- Log File: remoteops_072.log

## Project Overview

RemoteOps is a TCP/IP based remote system monitoring and management tool.
The Agent runs as a TCP server and the Controller connects as a TCP client.
Multiple Controllers are supported using a pthread-per-connection model.

A secondary UDP channel is used for periodic monitoring statistics.

## Main Features

- AUTH authentication before management commands
- SYSINFO system information
- LISTPROC process snapshot
- Restricted EXEC commands:
  - DATE
  - UPTIME
  - DISKFREE
  - HOSTNAME
  - WHOAMI
- PUT file upload
- GET file download
- UDP MONITOR START and STOP
- Graceful client disconnect handling
- Timestamped connection, command and transfer logging
- Multiple simultaneous Controllers
- Transfer throughput measurement for PUT and GET

## Build

Use the personalized Makefile:

    make -f Makefile_072

To remove compiled binaries:

    make -f Makefile_072 clean

## Run

Start the Agent:

    ./agent_072

Start a Controller from another terminal:

    ./controller_072 127.0.0.1 9411

The Agent listens on TCP port 9410.

## Personalised Files

- agent_072.c
- controller_072.c
- Makefile_072

## Testing

The project has been tested for:

- Authentication and command validation
- Invalid EXEC command rejection
- Five simultaneous Controllers
- PUT and GET byte-for-byte file transfer
- UDP monitoring START/STOP
- Graceful disconnect
- Timestamped logging
- PUT/GET transfer throughput logging

Validation results are recorded in:

    validation_tests_072.txt
