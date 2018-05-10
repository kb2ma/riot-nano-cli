## About

This application provides command line access to nanocoap, an API for
CoAP messaging. See the [CoAP spec][1] for background.

We support two setup options for this example:

### Native networking

Build with the standard `Makefile`. Follow the setup [instructions][2] for
the gnrc_networking example.

### SLIP-based border router

Build with `Makefile.slip`. Follow the setup instructions in README-slip.md,
which are based on the [SLIP instructions][3] for the gnrc_border_router
example. We also plan to provide or reference the ethos/UHCP instructions,
but we don't have it working yet.


## Example Use

Start a libcoap example server with the command below.

    ./coap-server

Enter the query below in the RIOT CLI.

    > coap get fe80::d8b8:65ff:feee:121b%6 5683 /.well-known/core

CLI output:

    nanocoap_cli: sending msg ID 1, 75 bytes
    > nanocoap: response Success, code 2.05, 105 bytes
    </>;title="General Info";ct=0,</time>;if="clock";rt="Ticks";title="Internal Clock";ct=0;obs,</async>;ct=0


[1]: https://tools.ietf.org/html/rfc7252    "CoAP spec"
[2]: https://github.com/RIOT-OS/RIOT/tree/master/examples/gnrc_networking    "instructions"
[3]: https://github.com/RIOT-OS/RIOT/tree/master/examples/gnrc_border_router    "SLIP instructions"
