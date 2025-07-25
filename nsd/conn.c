/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The Initial Developer of the Original Code and related documentation
 * is America Online, Inc. Portions created by AOL are Copyright (C) 1999
 * America Online, Inc. All Rights Reserved.
 *
 */


/*
 * conn.c --
 *
 *      Manage the Ns_Conn structure
 */

#include "nsd.h"

static Ns_ObjvValueRange posintRange0 = {0, INT_MAX};
static Ns_ObjvValueRange posSizeRange0 = {0, TCL_SIZE_MAX};
static Ns_ObjvValueRange posSizeRange1 = {1, TCL_SIZE_MAX};

/*
 * Static functions defined in this file.
 */

static int GetChan(Tcl_Interp *interp, const char *id, Tcl_Channel *chanPtr)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2) NS_GNUC_NONNULL(3);

static Tcl_Channel MakeConnChannel(const NsInterp *itPtr, Ns_Conn *conn)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2);

static const Ns_Driver* ConnGetDriver(const Ns_Conn *conn) NS_GNUC_PURE
    NS_GNUC_NONNULL(1);

static int ConnNoArg(int opt, unsigned int required_flags, Conn *connPtr,
                     NsInterp *itPtr, TCL_SIZE_T objc, Tcl_Obj *const* objv)
    NS_GNUC_NONNULL(4) NS_GNUC_NONNULL(6);

static char *DStringAppendConnFlags(Tcl_DString *dsPtr, unsigned int flags)
    NS_GNUC_NONNULL(1);


/*
 *----------------------------------------------------------------------
 *
 * DStringAppendConnFlags --
 *
 *      Decode a connection‐flags bitmask into a human‐readable string
 *      of flag names separated by '|' and append it to a Tcl_DString.
 *
 * Parameters:
 *      dsPtr – pointer to an initialized Tcl_DString to append to (must not be NULL)
 *      flags – bitmask of NS_CONN_* flags to decode
 *
 * Returns:
 *      A pointer to the internal string buffer of dsPtr (i.e., dsPtr->string),
 *      containing the appended flag names.
 *
 * Side Effects:
 *      Appends the textual representations of each set flag to the
 *      Tcl_DString, inserting '|' between multiple flags.
 *
 *----------------------------------------------------------------------
 */
static char *
DStringAppendConnFlags(Tcl_DString *dsPtr, unsigned int flags)
{
    int    count = 0;
    size_t i;
    static const struct {
        unsigned int state;
        const char  *label;
    } options[] = {
        { NS_CONN_CLOSED,            "SOCK_CLOSED" },
        { NS_CONN_SKIPHDRS,          "SKIPHDRS" },
        { NS_CONN_SKIPBODY,          "SKIPBODY" },
        { NS_CONN_READHDRS,          "READHDRS" },
        { NS_CONN_SENTHDRS,          "SENTHDRS" },
        { NS_CONN_WRITE_ENCODED,     "WRITE_ENCODED" },
        { NS_CONN_STREAM,            "STREAM" },
        { NS_CONN_STREAM_CLOSE,      "STREAM_CLOSE" },
        { NS_CONN_CHUNK,             "CHUNK" },
        { NS_CONN_SENT_LAST_CHUNK,   "SENT_LAST_CHUNK" },
        { NS_CONN_SENT_VIA_WRITER,   "SENT_VIA_WRITER" },
        { NS_CONN_SOCK_CORKED,       "SOCK_CORKED" },
        { NS_CONN_SOCK_WAITING,      "SOCK_WAITING" },
        { NS_CONN_ZIPACCEPTED,       "ZIPACCEPTED" },
        { NS_CONN_BROTLIACCEPTED,    "BROTLIACCEPTED" },
        { NS_CONN_CONTINUE,          "CONTINUE" },
        { NS_CONN_ENTITYTOOLARGE,    "ENTITYTOOLARGE" },
        { NS_CONN_REQUESTURITOOLONG, "REQUESTURITOOLONG" },
        { NS_CONN_LINETOOLONG,       "LINETOOLONG" },
        { NS_CONN_CONFIGURED,        "CONFIGURED" },
        { NS_CONN_SSL_WANT_WRITE,    "SSL_WANT_WRITE" },
    };

    NS_NONNULL_ASSERT(dsPtr != NULL);

    for (i = 0; i<sizeof(options) / sizeof(options[0]); i++) {
        if ((options[i].state & flags) != 0u) {
            if (count > 0) {
                Tcl_DStringAppend(dsPtr, "|", 1);
            }
            Tcl_DStringAppend(dsPtr, options[i].label, TCL_INDEX_NONE);
            count ++;
        }
    }
    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnHeaders --
 *
 *      Get the headers
 *
 * Results:
 *      An Ns_Set containing HTTP headers from the client
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_ConnHeaders(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);
    return conn->headers;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnOutputHeaders --
 *
 *      Get the output headers
 *
 * Results:
 *      A writable Ns_Set containing headers to send back to the client
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_ConnOutputHeaders(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);
    return conn->outputheaders;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnAuth --
 *
 *      Get the authentication headers
 *
 * Results:
 *      An Ns_Set containing authentication user/password and other parameters
 *      as in digest method
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_ConnAuth(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);
    return conn->auth;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnAuthUser --
 *
 *      Get the authenticated user
 *
 * Results:
 *      A pointer to a string with the username or NULL
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnAuthUser(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);
    return (conn->auth != NULL) ? Ns_SetIGet(conn->auth, "username") : NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnAuthPasswd --
 *
 *      Get the authenticated user's password
 *
 * Results:
 *      A pointer to a string with the user's plaintext password
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnAuthPasswd(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);
    return (conn->auth != NULL) ? Ns_SetIGet(conn->auth, "password") : NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContentLength --
 *
 *      Get the content length from the client
 *
 * Results:
 *      A size_t content length, or 0u if no data was sent
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

size_t
Ns_ConnContentLength(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);
    return conn->contentLength;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContent --
 *
 *      Return pointer to start of content.
 *
 * Results:
 *      Start of content.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnContent(const Ns_Conn *conn)
{
    const Conn *connPtr = (const Conn *) conn;

    return connPtr->reqPtr->content;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContentSize --
 *
 *      Return size of the posted content.
 *
 * Results:
 *      Size of the content buffer
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

size_t
Ns_ConnContentSize(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *)conn)->reqPtr->length;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContentFile --
 *
 *      Return pointer of the filename with spooled content.
 *
 * Results:
 *      Pointer to string
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnContentFile(const Ns_Conn *conn)
{
    const Sock *sockPtr;

    NS_NONNULL_ASSERT(conn != NULL);

    sockPtr = ((const Conn *)conn)->sockPtr;
    return (sockPtr != NULL) ? sockPtr->tfile : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContentFd --
 *
 *      Return opened file descriptor of the file with spooled content.
 *
 * Results:
 *      File descriptor or 0 if not used
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnContentFd(const Ns_Conn *conn)
{
    const Sock *sockPtr;

    NS_NONNULL_ASSERT(conn != NULL);

    sockPtr = ((const Conn *)conn)->sockPtr;

    return (sockPtr != NULL) ? sockPtr->tfd : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnServer --
 *
 *      Get the server name
 *
 * Results:
 *      A string ptr to the server name
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnServer(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *)conn)->server;
}

Ns_Server *
Ns_ConnServPtr(const Ns_Conn *conn)
{
    const Conn *connPtr = (Conn *)conn;

    return (Ns_Server *)(connPtr->sockPtr
                         ? connPtr->sockPtr->servPtr
                         :  NsGetServer(connPtr->server));
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnResponseStatus, Ns_ConnSetResponseStatus --
 *
 *      Get (set) the HTTP response code that will be sent.
 *
 * Results:
 *      An integer response code (e.g., 200 for OK).
 *
 * Side effects:
 *      NB: Status 200 is the default and can not be set manually.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnResponseStatus(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *)conn)->responseStatus;

}

void
Ns_ConnSetResponseStatus(Ns_Conn *conn, int newStatus)
{
    NS_NONNULL_ASSERT(conn != NULL);

    if (newStatus != 200) {
        ((Conn *)conn)->responseStatus = newStatus;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContentSent --
 *
 *      Return the number of bytes sent to the browser after headers
 *
 * Results:
 *      Bytes sent
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

size_t
Ns_ConnContentSent(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *)conn)->nContentSent;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetContentSent --
 *
 *      Set the number of bytes sent to the browser after headers
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void
Ns_ConnSetContentSent(Ns_Conn *conn, size_t length)
{
    NS_NONNULL_ASSERT(conn != NULL);

    ((Conn *)conn)->nContentSent = length;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnResponseLength --
 *
 *      Get the response length
 *
 * Results:
 *      Integer, number of bytes to send
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

ssize_t
Ns_ConnResponseLength(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *)conn)->responseLength;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPeerAddr, Ns_ConnForwardedPeerAddr --
 *
 *      Get the peer's direct or forwarded IP address.
 *      The forwarded IP address is determined by the
 *      x-forwarded-for header.
 *
 * Results:
 *      A string IP address
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnPeerAddr(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *)conn)->peer;
}

const char *
Ns_ConnForwardedPeerAddr(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *)conn)->proxypeer;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPeerAddr, Ns_ConnForwardedPeerAddr --
 *
 *      This is a mode specific function for determining the IP address of the
 *      communication peer. In reverse proxy mode, try to get the forwarded IP
 *      address.  In case, this fails, return the direct IP address. When
 *      reverse proxy mode is turned off, return the direct IP address.
 *
 * Results:
 *      A string IP address
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
const char *
Ns_ConnConfiguredPeerAddr(const Ns_Conn *conn)
{
    const char *p;

    NS_NONNULL_ASSERT(conn != NULL);

    if (nsconf.reverseproxymode.enabled) {
        p = Ns_ConnForwardedPeerAddr(conn);
        if (*p == '\0') {
            p = Ns_ConnPeerAddr(conn);
        }
    } else {
        p = Ns_ConnPeerAddr(conn);
    }
    return p;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnCurrentAddr --
 *
 *      Get the local IP address of the current connection
 *
 * Results:
 *      A string IP address or NULL
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
const char *
Ns_ConnCurrentAddr(const Ns_Conn *conn)
{
    const char *result;
    const Conn *connPtr;

    NS_NONNULL_ASSERT(conn != NULL);

    connPtr = (Conn *)conn;
    if (connPtr->sockPtr != NULL) {
        result = Ns_SockGetAddr((Ns_Sock *)connPtr->sockPtr);
    } else {
        result = NULL;
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnCurrentPort --
 *
 *      Get the local port of the current connection
 *
 * Results:
 *      port or 0 when operation fails
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
unsigned short
Ns_ConnCurrentPort(const Ns_Conn *conn)
{
    unsigned short result;
    const Conn    *connPtr;

    NS_NONNULL_ASSERT(conn != NULL);

    connPtr = (const Conn *)conn;
    if (connPtr->sockPtr != NULL) {
        result = Ns_SockGetPort((Ns_Sock *)(connPtr->sockPtr));
    } else {
        result = 0u;
    }
    return result;
}

#ifdef NS_WITH_DEPRECATED
/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPeer --
 *
 *      Get the peer's internet address, deprecated version of
 *      Ns_ConnPeerAddr().
 *
 *      Deprecated: Use Ns_ConnPeerAddr() for naming symmetry with
 *      the variants without "Peer" in the name.
 *
 * Results:
 *      A string IP address
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnPeer(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return Ns_ConnPeerAddr(conn);
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetPeer --
 *
 *      Set the peer's internet address and port
 *
 * Results:
 *      A string IP address
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnSetPeer(Ns_Conn *conn, const struct sockaddr *saPtr, const struct sockaddr *clientsaPtr)
{
    Conn *connPtr;

    NS_NONNULL_ASSERT(conn != NULL);
    NS_NONNULL_ASSERT(saPtr != NULL);
    NS_NONNULL_ASSERT(clientsaPtr != NULL);

    connPtr = (Conn *)conn;

    connPtr->reqPtr->port = Ns_SockaddrGetPort(saPtr);
    (void)ns_inet_ntop(saPtr, connPtr->peer, NS_IPADDR_SIZE);

    if (clientsaPtr->sa_family != 0) {
        (void)ns_inet_ntop(clientsaPtr, connPtr->proxypeer, NS_IPADDR_SIZE);
    } else {
        connPtr->proxypeer[0] = '\0';
    }

    return connPtr->peer;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPeerPort --
 *
 *      Get the port from which the peer is coming
 *
 * Results:
 *      The port number.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

unsigned short
Ns_ConnPeerPort(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *)conn)->reqPtr->port;
}


/*
 *----------------------------------------------------------------------
 * Ns_SetConnLocationProc --
 *
 *      Set pointer to custom routine that acts like
 *      Ns_ConnLocationAppend();
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      Overrides an old-style Ns_LocationProc.
 *
 *----------------------------------------------------------------------
 */

Ns_ReturnCode
Ns_SetConnLocationProc(Ns_ConnLocationProc *proc, Ns_TclCallback *cbPtr)
{
    Ns_ReturnCode status = NS_OK;
    NsServer     *servPtr = NsGetInitServer();

    NS_NONNULL_ASSERT(proc != NULL);
    NS_NONNULL_ASSERT(cbPtr != NULL);

    if (servPtr == NULL) {
        Ns_Log(Error, "Ns_SetConnLocationProc: no initializing server");
        status = NS_ERROR;
    } else {
        servPtr->vhost.connLocationProc = proc;
        servPtr->vhost.connLocationArg = cbPtr;
    }

    return status;
}

#ifdef NS_WITH_DEPRECATED
/*
 *----------------------------------------------------------------------
 * Ns_SetLocationProc --
 *
 *      Set pointer to custom routine that acts like Ns_ConnLocation();
 *
 *      Deprecated: Use Ns_SetConnLocationProc() which is virtual host
 *      aware.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void
Ns_SetLocationProc(const char *server, Ns_LocationProc *proc)
{
    NsServer *servPtr = NsGetServer(server);

    NS_NONNULL_ASSERT(server != NULL);
    NS_NONNULL_ASSERT(proc != NULL);

    if (servPtr != NULL) {
        servPtr->vhost.locationProc = proc;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnLocation --
 *
 *      Get the location according to the driver for this connection.
 *      It is of the form SCHEME://HOSTNAME:PORT
 *
 *      Deprecated: Use Ns_ConnLocationAppend() which is virtual host
 *      aware.
 *
 * Results:
 *      A string URL, not including path
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnLocation(Ns_Conn *conn)
{
    const Conn     *connPtr = (const Conn *) conn;
    const NsServer *servPtr = connPtr->poolPtr->servPtr;
    const char     *location = NULL;

    if (servPtr->vhost.locationProc != NULL) {
        /*
         * Call the registered proc which is typically, a Tcl
         * call. Therefore, make sure, the connection has already an
         * interpreter associated.
         */
        Ns_GetConnInterp(conn);

        location = (*servPtr->vhost.locationProc)(conn);
    }
    if (location == NULL) {
        location = connPtr->location;
    }

    return location;
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnLocationAppend --
 *
 *      Append the location of this connection to dest. It is of the
 *      form SCHEME://HOSTNAME:PORT
 *
 * Results:
 *      dest->string.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnLocationAppend(Ns_Conn *conn, Tcl_DString *dest)
{
    const Conn     *connPtr;
    const NsServer *servPtr;
    const Ns_Set   *headers;
    const char     *host = NULL;
    char           *location = NULL;

    NS_NONNULL_ASSERT(conn != NULL);
    NS_NONNULL_ASSERT(dest != NULL);

    connPtr = ((Conn *)conn);
    assert(connPtr->poolPtr != NULL);

    servPtr = connPtr->poolPtr->servPtr;
    assert(servPtr != NULL);

    Ns_Log(Debug, "Ns_ConnLocation: connLocationProc %s vhost.enabled %d behind revproxy %d",
           servPtr->vhost.connLocationProc == NULL ? "NOT PROVIDED" : "PROVIDED",
           servPtr->vhost.enabled,
           nsconf.reverseproxymode.enabled);

    if (servPtr->vhost.connLocationProc != NULL) {
        /*
         * Prefer the new style Ns_ConnLocationProc.
         *
         * Call the registered proc which is typically, a Tcl
         * call. Therefore, make sure, the connection has already an
         * interpreter associated.
         */
        Ns_GetConnInterp(conn);

        location = (*servPtr->vhost.connLocationProc)(conn, dest, servPtr->vhost.connLocationArg);
        Ns_Log(Debug, "Ns_ConnLocation: locationproc returned <%s>", location);

#ifdef NS_WITH_DEPRECATED
    } else if (servPtr->vhost.locationProc != NULL) {
        /*
         * Fall back to old style Ns_LocationProc.
         */
        Ns_GetConnInterp(conn);

        location = (*servPtr->vhost.locationProc)(conn);
        if (location != NULL) {
            location = Tcl_DStringAppend(dest, location, TCL_INDEX_NONE);
            Ns_Log(Debug, "Ns_ConnLocation: old style locationproc returned <%s>", location);
        }
#endif
    } else if (servPtr->vhost.enabled
               && ((headers = Ns_ConnHeaders(conn)) != NULL)
               && ((host = Ns_SetIGet(headers, "host")) != NULL)
               && (*host != '\0')
               && Ns_StrIsValidHostHeaderContent(host)) {
        /*
         * NaviServer "vhosting" is enabled, and host header field is given
         * and syntactically valid. Construct a location string based on
         * driver information. Do not append an extra port (must be included
         * in "host" if necessary).
         */
        location = Ns_HttpLocationString(dest, connPtr->drvPtr->protocol, host, 0u, 0u);
        Ns_Log(Debug, "Ns_ConnLocation: vhost - location based on host header field <%s>", location);
    } else if (nsconf.reverseproxymode.enabled
               && ((headers = Ns_ConnHeaders(conn)) != NULL)
               && ((host = Ns_SetIGet(headers, "host")) != NULL)
               && (*host != '\0')) {
        /*
         * NaviServer "reverseproxymode" is enabled, and host header field is
         * given. The field content is checked against the hash table of valid
         * host header fields. Do not append an extra port (must be included
         * in "host" if necessary).
         */
        const Tcl_HashEntry  *hPtr = Tcl_FindHashEntry((Tcl_HashTable *)&servPtr->hosts, host);
        if (hPtr != NULL) {
            location = Ns_HttpLocationString(dest, connPtr->drvPtr->protocol, host, 0u, 0u);
            Ns_Log(Debug, "Ns_ConnLocation: reverseproxymode location <%s>", location);
        }
    } else if (servPtr->vhost.enabled || nsconf.reverseproxymode.enabled) {
        /*
         * When relying on the "host" header fields, but it is invalid or not
         * provided, complain about this in the log file.
         */
        if (host != NULL) {
            Ns_Log(Warning, "Ns_ConnLocation: ignore invalid or untrusted host header field: '%s'", host);
        } else {
            Ns_Log(Warning, "Ns_ConnLocation: required host header field is missing");
        }
    }

    /*
     * If everything above failed, try the location from the connPtr. This is
     * actually determine from sockPtr->location which comes from
     * mapPtr->location, which comes from the virtual hosts mapping table.
     */
    if ((location == NULL) && (connPtr->location != NULL)) {
        location = Tcl_DStringAppend(dest, connPtr->location, TCL_INDEX_NONE);
        Ns_Log(Debug, "Ns_ConnLocation: location from mapping table <%s>", connPtr->location);
    }

    /*
     * If everything above failed, try the static driver location or - as last
     * resort - use the configured address.
     */

    if (location == NULL) {
        unsigned short port;
        const char    *addr;

        if (connPtr->sockPtr != NULL) {
            port = Ns_SockGetPort((Ns_Sock*)connPtr->sockPtr);
            addr = Ns_SockGetAddr((Ns_Sock*)connPtr->sockPtr);
        } else {
            port = connPtr->drvPtr->port;
            addr = connPtr->drvPtr->address;
        }
        Ns_Log(Debug, "Ns_ConnLocation: final resort, use numerical address '%s' '%hu'",
               addr, port);
        location = Ns_HttpLocationString(dest, connPtr->drvPtr->protocol,
                                         addr, port, connPtr->drvPtr->defport);
    }
    Ns_Log(Debug, "Ns_ConnLocation: final value '%s'", location);

    return location;
}

/*
 *----------------------------------------------------------------------
 *
 * ConnGetDriver --
 *
 *      Return the Driver for a connection
 *
 * Results:
 *      configured driver
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
static const Ns_Driver*
ConnGetDriver(const Ns_Conn *conn)
{
    return (Ns_Driver *)((const Conn *)conn)->drvPtr;
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnHost --
 *
 *      Get the address of the current connection
 *
 * Results:
 *      A string address
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnHost(const Ns_Conn *conn)
{
    const Ns_Driver *drvPtr;

    NS_NONNULL_ASSERT(conn != NULL);

    drvPtr = ConnGetDriver(conn);
    assert(drvPtr != NULL);

    return drvPtr->address;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPort --
 *
 *      What server port is this connection on?
 *
 * Results:
 *      Integer port number
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

unsigned short
Ns_ConnPort(const Ns_Conn *conn)
{
    const Driver *drvPtr;

    NS_NONNULL_ASSERT(conn != NULL);

    drvPtr = ((const Conn *)conn)->drvPtr;
    assert(drvPtr != NULL);

    return drvPtr->port;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSock --
 *
 *      Return the underlying socket for a connection.
 *
 * Results:
 *      socket descriptor
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

NS_SOCKET
Ns_ConnSock(const Ns_Conn *conn)
{
    const Sock *sockPtr;

    NS_NONNULL_ASSERT(conn != NULL);

    sockPtr = ((const Conn *)conn)->sockPtr;

    return (sockPtr != NULL) ? sockPtr->sock : NS_INVALID_SOCKET;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSockPtr --
 *
 *      Return the underlying socket for a connection.
 *
 * Results:
 *      Ns_sock struct
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Ns_Sock *
Ns_ConnSockPtr(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return (Ns_Sock *)(((const Conn *)conn))->sockPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSockContent --
 *
 *      Returns read buffer for incoming requests.
 *
 * Results:
 *      NULL if no content have been read yet
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Tcl_DString *
Ns_ConnSockContent(const Ns_Conn *conn)
{
    Request    *reqPtr;
    Tcl_DString *result;

    NS_NONNULL_ASSERT(conn != NULL);

    reqPtr = ((const Conn *)conn)->reqPtr;
    if (likely(reqPtr != NULL)) {
        result = &(reqPtr->buffer);
    } else {
        result = NULL;
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnDriverName --
 *
 *      Return the name of this driver
 *
 * Results:
 *      A driver name
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnDriverName(const Ns_Conn *conn)
{
    const Ns_Driver *drvPtr;

    NS_NONNULL_ASSERT(conn != NULL);

    drvPtr = ConnGetDriver(conn);
    assert(drvPtr != NULL);

    return drvPtr->moduleName;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnStartTime, Ns_ConnAcceptTime, Ns_ConnQueueTime, Ns_ConnDequeueTime,
 * Ns_ConnFilterTime --
 *
 *      Ns_ConnStartTime() returns the connection start time, which is the
 *      time the connection was queued from the driver thread (not to be
 *      mistaken as the time the underlying socket was opened to the server).
 *
 *      The following functions have the identical interface:
 *
 *        - Ns_ConnAcceptTime() returns the time the connection was accepted
 *          (this is maybe a kept open connection),
 *        - Ns_ConnQueueTime() returns the time a request was queued,
 *        - Ns_ConnDequeueTime() returns the time a request was taken out of
 *          the queue, and
 *        - Ns_ConnFilterTime() is the timestamp after the filters are executed.
 *
 * Results:
 *      Ns_Time pointer.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Ns_Time *
Ns_ConnStartTime(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    NS_NONNULL_ASSERT(conn != NULL);
    return &connPtr->requestQueueTime;
}

Ns_Time *
Ns_ConnAcceptTime(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    NS_NONNULL_ASSERT(conn != NULL);
    return &connPtr->acceptTime;
}

Ns_Time *
Ns_ConnQueueTime(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    NS_NONNULL_ASSERT(conn != NULL);
    return &connPtr->requestQueueTime;
}

Ns_Time *
Ns_ConnDequeueTime(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    NS_NONNULL_ASSERT(conn != NULL);
    return &connPtr->requestDequeueTime;
}

Ns_Time *
Ns_ConnFilterTime(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    NS_NONNULL_ASSERT(conn != NULL);
    return &connPtr->filterDoneTime;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnTimeSpans --
 *
 *      Return for a given connection the time spans computed by
 *      Ns_ConnTimeStats()
 *
 * Results:
 *      Four time structures (argument 2 to 5)
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
void
Ns_ConnTimeSpans(const Ns_Conn *conn,
                 Ns_Time *acceptTimeSpanPtr, Ns_Time *queueTimeSpanPtr,
                 Ns_Time *filterTimeSpanPtr, Ns_Time *runTimeSpanPtr) {
    const Conn *connPtr;

    NS_NONNULL_ASSERT(conn != NULL);
    NS_NONNULL_ASSERT(acceptTimeSpanPtr != NULL);
    NS_NONNULL_ASSERT(queueTimeSpanPtr != NULL);
    NS_NONNULL_ASSERT(filterTimeSpanPtr != NULL);
    NS_NONNULL_ASSERT(runTimeSpanPtr != NULL);

    connPtr = (const Conn *) conn;
    *acceptTimeSpanPtr = connPtr->acceptTimeSpan;
    *queueTimeSpanPtr  = connPtr->queueTimeSpan;
    *filterTimeSpanPtr = connPtr->filterTimeSpan;
    *runTimeSpanPtr    = connPtr->runTimeSpan;
}


/*
 *----------------------------------------------------------------------
 *
 * NsConnTimeStatsUpdate --
 *
 *      Compute for a given connection various time spans such as
 *      acceptTimeSpan, queueTimeSpan, filterTimeSpan and
 *      runTimeSpan as follows
 *
 *         acceptTimeSpan = queueTime - acceptTime
 *         queueTimeSpan  = dequeueTime - queueTime
 *         filterTimeSpan = filterDoneTime - dequeueTime
 *         runTimeSpan    = runDoneTime - filterDoneTime
 *
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Update time span values in connPtr
 *
 *----------------------------------------------------------------------
 */
void
NsConnTimeStatsUpdate(Ns_Conn *conn) {
    Conn     *connPtr;

    NS_NONNULL_ASSERT(conn != NULL);

    connPtr = (Conn *) conn;

    Ns_GetTime(&connPtr->runDoneTime);

    (void)Ns_DiffTime(&connPtr->requestQueueTime,   &connPtr->acceptTime,         &connPtr->acceptTimeSpan);
    (void)Ns_DiffTime(&connPtr->requestDequeueTime, &connPtr->requestQueueTime,   &connPtr->queueTimeSpan);
    (void)Ns_DiffTime(&connPtr->filterDoneTime,     &connPtr->requestDequeueTime, &connPtr->filterTimeSpan);
    (void)Ns_DiffTime(&connPtr->runDoneTime,        &connPtr->filterDoneTime,     &connPtr->runTimeSpan);

}


/*
 *----------------------------------------------------------------------
 *
 * NsConnTimeStatsFinalize --
 *
 *      Record the time after running the connection main task and the end of
 *      the processing of this task called traceTimeSpan. This value is
 *      calculated as follows:
 *
 *         traceTimeSpan  = now - runDoneTime
 *
 *      In addition, this function updates the statistics and should
 *      be called only once per request.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Update statistics.
 *
 *----------------------------------------------------------------------
 */
void
NsConnTimeStatsFinalize(const Ns_Conn *conn) {
    const Conn *connPtr;
    ConnPool   *poolPtr;
    Ns_Time     now, diffTimeSpan;

    NS_NONNULL_ASSERT(conn != NULL);

    connPtr = (const Conn *) conn;
    poolPtr = connPtr->poolPtr;
    assert(poolPtr != NULL);

    Ns_GetTime(&now);

    (void)Ns_DiffTime(&now, &connPtr->runDoneTime, &diffTimeSpan);

    Ns_MutexLock(&poolPtr->threads.lock);
    Ns_IncrTime(&poolPtr->stats.acceptTime, connPtr->acceptTimeSpan.sec, connPtr->acceptTimeSpan.usec);
    Ns_IncrTime(&poolPtr->stats.queueTime,  connPtr->queueTimeSpan.sec,  connPtr->queueTimeSpan.usec);
    Ns_IncrTime(&poolPtr->stats.filterTime, connPtr->filterTimeSpan.sec, connPtr->filterTimeSpan.usec);
    Ns_IncrTime(&poolPtr->stats.runTime,    connPtr->runTimeSpan.sec,    connPtr->runTimeSpan.usec);
    Ns_IncrTime(&poolPtr->stats.traceTime,  diffTimeSpan.sec,            diffTimeSpan.usec);
    Ns_MutexUnlock(&poolPtr->threads.lock);
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnTimeout --
 *
 *      Absolute time value beyond which conn should not wait on
 *      resources, such as condition variables.
 *
 * Results:
 *      Ns_Time pointer.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Ns_Time *
Ns_ConnTimeout(Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return &((Conn *)conn)->timeout;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnId --
 *
 *      Return the connection id.
 *
 * Results:
 *      The connection id.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

uintptr_t
Ns_ConnId(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *)conn)->id;
}

const char *
NsConnIdStr(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *)conn)->idstr;
}




/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnModifiedSince --
 *
 *      Has the data the url points to changed since a given time?
 *
 * Results:
 *      NS_TRUE if data modified, NS_FALSE otherwise.
 *
 * Side effects:
 *      None
 *
 * NOTE: This doesn't do a strict time check.  If the server flags aren't
 *       set to check modification, or if there wasn't an 'If-Modified-Since'
 *       header in the request, then this'll always return true.
 *
 *----------------------------------------------------------------------
 */

bool
Ns_ConnModifiedSince(const Ns_Conn *conn, time_t since)
{
    const ConnPool *poolPtr;
    bool            result = NS_TRUE;

    NS_NONNULL_ASSERT(conn != NULL);

    poolPtr = ((const Conn *)conn)->poolPtr;
    assert(poolPtr != NULL);
    assert(poolPtr->servPtr != NULL);

    if (poolPtr->servPtr->opts.modsince) {
        const char *hdr = Ns_SetIGet(conn->headers, "if-modified-since");

        if ((hdr != NULL) && (Ns_ParseHttpTime(hdr) >= since)) {
            result = NS_FALSE;
        }
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnUnmodifiedSince --
 *
 *      Has the data the url points to changed since a given time?
 *
 * Results:
 *      NS_TRUE if data unmodified or header not present, NS_FALSE otherwise.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

bool
Ns_ConnUnmodifiedSince(const Ns_Conn *conn, time_t since)
{
    const char *hdr;
    bool        result = NS_TRUE;

    hdr = Ns_SetIGet(conn->headers, "if-unmodified-since");
    if ((hdr != NULL) && (Ns_ParseHttpTime(hdr) < since)) {
        result = NS_FALSE;
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnGetEncoding, Ns_ConnSetEncoding --
 *
 *      Get (set) the Tcl_Encoding for the connection which is used
 *      to convert from UTF to specified output character set.
 *
 * Results:
 *      Pointer to Tcl_Encoding (get) or NULL (set).
 *
 * Side effects:
 *      See Ns_ConnGetQuery().
 *
 *----------------------------------------------------------------------
 */

Tcl_Encoding
Ns_ConnGetEncoding(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *) conn)->outputEncoding;
}

void
Ns_ConnSetEncoding(Ns_Conn *conn, Tcl_Encoding encoding)
{
    NS_NONNULL_ASSERT(conn != NULL);

    ((Conn *) conn)->outputEncoding = encoding;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnGetUrlEncoding, Ns_ConnSetUrlEncoding --
 *
 *      Get (set) the Tcl_Encoding for the connection which is used
 *      to convert input forms to proper UTF.
 *
 * Results:
 *      Pointer to Tcl_Encoding (get) or NULL (set).
 *
 * Side effects:
 *      See Ns_ConnGetQuery().
 *
 *----------------------------------------------------------------------
 */

Tcl_Encoding
Ns_ConnGetUrlEncoding(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *) conn)->urlEncoding;
}

void
Ns_ConnSetUrlEncoding(Ns_Conn *conn, Tcl_Encoding encoding)
{
    NS_NONNULL_ASSERT(conn != NULL);

    ((Conn *) conn)->urlEncoding = encoding;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnGetCompression, Ns_ConnSetCompression
 *
 *      Get configured compression level (*Get*) or enable/disable compression
 *      with the specified level (*Set*).  Output will only be compressed if
 *      client advertises support.
 *
 *      Level 1 is 'on' i.e. default compression from config.
 *
 * Results:
 *      Compression level, 0-9.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnGetCompression(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return ((const Conn *) conn)->requestCompress;
}

void
Ns_ConnSetCompression(Ns_Conn *conn, int level)
{
    NS_NONNULL_ASSERT(conn != NULL);

#ifdef HAVE_ZLIB_H
    ((Conn *) conn)->requestCompress = MIN(MAX(level, 0), 9);
#else
    ((Conn *) conn)->requestCompress = 0;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnTarget --
 *
 *      Returns the URI target from the start line of the request.
 *
 * Results:
 *      String
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_ConnTarget(Ns_Conn *conn, Tcl_DString *dsPtr)
{
    const char *targetPtr;

    NS_NONNULL_ASSERT(conn != NULL);
    NS_NONNULL_ASSERT(dsPtr != NULL);

    Tcl_DStringAppend(dsPtr, conn->request.line, TCL_INDEX_NONE);

    /*
     * Skip non-spaces + spaces from the left (HTTP method).
     */
    targetPtr = strchr(dsPtr->string, INTCHAR(' '));
    if (targetPtr != NULL) {
        char *p;

        ++ targetPtr;
        /*
         * Skip non-spaces + space from the right (HTTP version)
         */
        p = strrchr(targetPtr, INTCHAR(' '));
        if (p != NULL) {
            *p = '\0';
        }
    } else {
        targetPtr = dsPtr->string;
    }
    return targetPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * ConnContentObjCmd --
 *
 *      Implements "ns_conn content"
 *
 * Results:
 *      Standard Tcl result
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int
ConnContentObjCmd(ClientData clientData, Tcl_Interp *interp, TCL_SIZE_T objc, Tcl_Obj *const* objv, unsigned int flags)
{
    NsInterp   *itPtr = clientData;
    Conn       *connPtr;
    int         result = TCL_OK, binary = (int)NS_FALSE;
    Tcl_WideInt given_length = -1, given_offset = 0;
    TCL_SIZE_T  length = TCL_INDEX_NONE, requiredLength, offset = 0;
    Tcl_DString encDs;
    Ns_ObjvSpec lopts[] = {
        {"-binary",    Ns_ObjvBool,  &binary, INT2PTR(NS_TRUE)},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"?offset", Ns_ObjvWideInt, &given_offset, &posSizeRange0},
        {"?length", Ns_ObjvWideInt, &given_length, &posSizeRange1},
        {NULL, NULL, NULL, NULL}
    };

    connPtr = (Conn *)itPtr->conn;

    if (Ns_ParseObjv(lopts, args, interp, 2, objc, objv) != NS_OK
        || NsConnRequire(interp, flags, NULL, &result) != NS_OK   ) {
        result = TCL_ERROR;

    } else if ((connPtr->flags & NS_CONN_CLOSED) != 0u) {
        /*
         * In cases, the content is allocated via mmap, the content
         * is unmapped when the socket is closed. Accessing the
         * content will crash the server. Although we might not have
         * the same problem when the content is allocated
         * differently, we use here the restrictive strategy to
         * provide consistent behavior independent of the allocation
         * strategy.
         */
        Ns_TclPrintfResult(interp, "connection already closed, can't get content");
        result = TCL_ERROR;
    }
    offset = (TCL_SIZE_T)given_offset;
    length = given_length == -1 ? TCL_INDEX_NONE : (TCL_SIZE_T)given_length;

    requiredLength = length;
    if ((result == TCL_OK)
        && (offset > 0)
        && ((size_t)offset > connPtr->reqPtr->length)
        ) {
        Ns_TclPrintfResult(interp, "offset exceeds available content length");
        result = TCL_ERROR;
    }

    if ((result == TCL_OK) && (length == TCL_INDEX_NONE)) {
        length = (TCL_SIZE_T)connPtr->reqPtr->length - offset;

    } else if ((result == TCL_OK)
               && (length >= 0)
               && (offset >= 0)
               && ((size_t)length + (size_t)offset > connPtr->reqPtr->length)
               ) {
        Ns_TclPrintfResult(interp, "offset (%" PRITcl_Size ") + length"
                           " (%" PRITcl_Size ") exceeds available content length"
                           " (%" PRIuz ")",
                           offset, length,
                           connPtr->reqPtr->length);
        result = TCL_ERROR;
    }

    if (result == TCL_OK) {
        size_t      contentLength;
        const char *content;

        if (connPtr->reqPtr->length == 0u) {
            content = NULL;
            contentLength = 0u;
            Tcl_ResetResult(interp);
        } else if (!binary) {
            content = Tcl_ExternalToUtfDString(connPtr->outputEncoding,
                                               connPtr->reqPtr->content,
                                               (TCL_SIZE_T)connPtr->reqPtr->length,
                                               &encDs);
            contentLength = (size_t)Tcl_DStringLength(&encDs);
            if (requiredLength == TCL_INDEX_NONE) {
                length = Tcl_DStringLength(&encDs) - offset;
            }
        } else {
            content = connPtr->reqPtr->content;
            contentLength = connPtr->reqPtr->length;
        }

        if (contentLength > 0u) {
            if (requiredLength == TCL_INDEX_NONE && offset == 0) {
                /*
                 * return full content
                 */
                if (!binary) {
                    Tcl_DStringResult(interp, &encDs);
                } else {
                    Tcl_SetObjResult(interp, Tcl_NewByteArrayObj((uint8_t*)connPtr->reqPtr->content,
                                                                 (TCL_SIZE_T)connPtr->reqPtr->length));
                }
            } else {
                /*
                 * return partial content
                 */
                if (!binary) {
                    Tcl_Obj *contentObj = Tcl_NewStringObj(content, (TCL_SIZE_T)contentLength);

                    Tcl_SetObjResult(interp, Tcl_GetRange(contentObj, offset, offset+length-1));
                    Tcl_DStringFree(&encDs);
                    Tcl_DecrRefCount(contentObj);
                } else {
                    Tcl_SetObjResult(interp, Tcl_NewByteArrayObj((const uint8_t*)content + offset,
                                                                 (TCL_SIZE_T)length));
                }
            }
        }
    }

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ConnCopyObjCmd --
 *
 *      Implements "ns_conn copy"
 *
 * Results:
 *      Standard Tcl result
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int
ConnCopyObjCmd(ClientData clientData, Tcl_Interp *interp, TCL_SIZE_T objc, Tcl_Obj *const* objv, unsigned int flags)
{
    int               result = TCL_OK;
    NsInterp         *itPtr = clientData;
    Conn             *connPtr;
    Tcl_Channel       chan;
    char             *channelString;
    Tcl_WideInt       offset, lengthValue;
    Ns_ObjvValueRange offsetRange = {0, TCL_SIZE_MAX};
    Ns_ObjvValueRange lengthRange = {0, TCL_SIZE_MAX};
    Ns_ObjvSpec args[] = {
        {"offset",    Ns_ObjvWideInt, &offset,       &offsetRange},
        {"length",    Ns_ObjvWideInt, &lengthValue,  &lengthRange},
        {"channelId", Ns_ObjvString,  &channelString, NULL},
        {NULL, NULL, NULL, NULL}
    };

    connPtr = (Conn *)itPtr->conn;

    if (Ns_ParseObjv(NULL, args, interp, 2, objc, objv) != NS_OK
        || NsConnRequire(interp, flags, NULL, &result) != NS_OK   ) {
        result = TCL_ERROR;

    } else if (GetChan(interp, channelString, &chan) != TCL_OK) {
        result = TCL_ERROR;

    } else if (offset > (Tcl_WideInt)connPtr->reqPtr->length) {
        Ns_TclPrintfResult(interp, "offset is larger than request length");
        result = TCL_ERROR;

    } else if (lengthValue > ((Tcl_WideInt)connPtr->reqPtr->length - offset)) {
        Ns_TclPrintfResult(interp, "length is larger than request length minus offset");
        result = TCL_ERROR;

    } else if (connPtr->reqPtr->content == NULL) {
        if (Ns_ConnContentFile(itPtr->conn) != NULL) {
            Ns_TclPrintfResult(interp, "content was spooled to a file, cannot use 'ns_conn copy'"
                               "in this situation; must be handled on the Tcl layer");
            result = TCL_ERROR;
        } else {
            Ns_Log(Warning, "No-op 'ns_conn copy': No content was uploaded, nothing to copy");
        }
    } else {
        TCL_SIZE_T length;
        char      *content = connPtr->reqPtr->content + offset;

        length = (TCL_SIZE_T)lengthValue;
#ifdef NS_SKIPBOM
        Ns_Log(Notice, "NS_CONN COPY offset %d length %d chan '%s'\n",
               offset, length, channelString);
        /*
         * The passed-in channel is binary. If this is the first
         * write operation, and file file starts with a BOM, then
         * strip it.
         */
        if (Tcl_Tell(chan) == 0 &&
            UCHAR(content[0]) == 0xEF &&
            UCHAR(content[1]) == 0xBB &&
            UCHAR(content[2]) == 0xBF) {
            Ns_Log(Notice, "NS_CONN COPY ---- BOM");
            content += 3;
            length -= 3;
        }
#endif
        if (Tcl_Write(chan, content, length) != length) {
            Ns_TclPrintfResult(interp, "could not write %ld bytes to %s: %s",
                               (long)length, channelString, Tcl_PosixError(interp));
            result = TCL_ERROR;
        }
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ConnFormObjCmd --
 *
 *      Implements "ns_conn form"
 *
 * Results:
 *      Standard Tcl result
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int
ConnFormObjCmd(ClientData clientData, Tcl_Interp *interp, TCL_SIZE_T objc, Tcl_Obj *const* objv, unsigned int flags)
{
    int         result = TCL_OK;
    NsInterp   *itPtr = clientData;
    Tcl_Obj    *fallbackCharsetObj = NULL;
    Ns_ObjvSpec lopts[] = {
        {"-fallbackcharset", Ns_ObjvObj, &fallbackCharsetObj, NULL},
        {NULL, NULL, NULL, NULL}
    };

    if (Ns_ParseObjv(lopts, NULL, interp, 2, objc, objv) != NS_OK
        || NsConnRequire(interp, flags, NULL, &result) != NS_OK ) {
        result = TCL_ERROR;

    } else if ((itPtr->nsconn.flags & CONN_TCLFORM) != 0u) {
        /*
         * We know, if the value has been computed already.
         */
        Tcl_SetResult(interp, itPtr->nsconn.form, TCL_STATIC);

    } else {
        Ns_ReturnCode rc = NS_OK;
        Ns_Set       *form = Ns_ConnGetQuery(interp, itPtr->conn, fallbackCharsetObj, &rc);

        if (rc == NS_ERROR) {
            /*
             * Ns_ConnGetQuery() provides error message when rc != NS_OK;
             */
            result = TCL_ERROR;

        } else if (form == NULL) {
            itPtr->nsconn.form[0] = '\0';
            itPtr->nsconn.flags |= CONN_TCLFORM;

        } else if (unlikely(Ns_TclEnterSet(interp, form, NS_TCL_SET_STATIC) != TCL_OK)) {
            result = TCL_ERROR;

        } else {
            TCL_SIZE_T setNameLength;
            const char *setName = Tcl_GetStringFromObj(Tcl_GetObjResult(interp), &setNameLength);

            setNameLength++;
            memcpy(itPtr->nsconn.form, setName, MIN((size_t)setNameLength, NS_SET_SIZE));
            itPtr->nsconn.flags |= CONN_TCLFORM;
        }
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * NsTclConnObjCmd --
 *
 *      Implements "ns_conn".
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

enum ISubCmdIdx {
    CAacceptedcompressionIdx, CAuthIdx, CAuthPasswordIdx, CAuthUserIdx,
    CChannelIdx, CClientdataIdx, CCloseIdx, CCompressIdx, CContentIdx,
    CContentFileIdx, CContentLengthIdx, CContentSentLenIdx, CCopyIdx,
    CCurrentAddrIdx, CCurrentPortIdx,
    CDetailsIdx, CDriverIdx,
    CEncodingIdx,
    CFileHdrIdx, CFileLenIdx, CFileOffIdx, CFilesIdx, CFlagsIdx, CFormIdx, CFragmentIdx,
    CHeaderLengthIdx, CHeadersIdx, CHostIdx,
    CIdIdx, CIsConnectedIdx,
    CKeepAliveIdx,
    CLocationIdx,
    CMethodIdx,
    COutputHeadersIdx,
    CPartialTimesIdx, CPeerAddrIdx, CPeerPortIdx, CPoolIdx, CPortIdx, CProtocolIdx,
    CQueryIdx,
    CRatelimitIdx, CRequestIdx,
    CServerIdx, CSockIdx, CStartIdx, CStatusIdx,
    CTargetIdx, CTimeoutIdx,
    CUrlIdx, CUrlcIdx, CUrlDictIdx, CUrlEncodingIdx, CUrlvIdx,
    CVersionIdx,
    CZipacceptedIdx
};

int
NsTclConnObjCmd(ClientData clientData, Tcl_Interp *interp, TCL_SIZE_T objc, Tcl_Obj *const* objv)
{
    NsInterp            *itPtr = clientData;
    Conn                *connPtr;
    Ns_Conn             *conn;
    const Ns_Request    *request = NULL;
    Tcl_Encoding         encoding;
    int                  opt = 0, result = TCL_OK;

    static const char *const opts[] = {
        "acceptedcompression", "auth", "authpassword", "authuser",
        "channel", "clientdata", "close", "compress", "content",
        "contentfile", "contentlength", "contentsentlength", "copy",
        "currentaddr", "currentport",
        "details", "driver",
        "encoding",
        "fileheaders", "filelength", "fileoffset", "files", "flags", "form", "fragment",
        "headerlength", "headers", "host",
        "id", "isconnected",
        "keepalive",
        "location",
        "method",
        "outputheaders",
        "partialtimes", "peeraddr", "peerport", "pool", "port", "protocol",
        "query",
        "ratelimit", "request",
        "server", "sock", "start", "status",
        "target", "timeout",
        "url", "urlc", "urldict", "urlencoding", "urlv",
        "version",
        "zipaccepted",
        NULL
    };
    static const unsigned int required_flags[] = {
        /* A */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED,
        /* line continued */ NS_CONN_REQUIRE_CONFIGURED,
        /* C */ NS_CONN_REQUIRE_OPEN, NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_OPEN,
        /* line continued */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED,
        /* C */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_OPEN,
        /* line continued */ NS_CONN_REQUIRE_OPEN,
        /* C */ NS_CONN_REQUIRE_CONNECTED, NS_CONN_REQUIRE_CONNECTED,
        /* D */ NS_CONN_REQUIRE_CONNECTED, NS_CONN_REQUIRE_CONFIGURED,
        /* E */ NS_CONN_REQUIRE_CONFIGURED,
        /* F */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED,
        /* line continued */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED,
        /* line continued */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED,
        /* H */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED,
        /* I */ NS_CONN_REQUIRE_CONFIGURED, 0u,
        /* K */ NS_CONN_REQUIRE_CONNECTED,
        /* L */ NS_CONN_REQUIRE_CONFIGURED,
        /* M */ NS_CONN_REQUIRE_CONFIGURED,
        /* O */ NS_CONN_REQUIRE_CONFIGURED,
        /* P */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED,
        /* line continued */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONNECTED,
        /* line continued */ NS_CONN_REQUIRE_CONFIGURED,
        /* Q */ NS_CONN_REQUIRE_CONFIGURED,
        /* R */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED,
        /* S */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONNECTED, NS_CONN_REQUIRE_CONFIGURED,
        /* line continued */ NS_CONN_REQUIRE_CONFIGURED,
        /* T */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED,
        /* U */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED,
        /* line continued */ NS_CONN_REQUIRE_CONFIGURED, NS_CONN_REQUIRE_CONFIGURED,
        /* V */ NS_CONN_REQUIRE_CONFIGURED,
        /* Z */ NS_CONN_REQUIRE_CONFIGURED,
        0u
    };

    assert(itPtr != NULL);
    conn = itPtr->conn;
    connPtr = (Conn *)conn;

    if (unlikely(objc < 2)) {
        Tcl_WrongNumArgs(interp, 1, objv, "/subcommand/");
        opt = (int)CIsConnectedIdx; /* silence static checker */
        result = TCL_ERROR;

    } else if (unlikely(Tcl_GetIndexFromObj(interp, objv[1], opts, "subcommand", 0,
                                            &opt) != TCL_OK)) {
        result = TCL_ERROR;
    }

    if (result != TCL_ERROR) {
        /*
         * The first group of commands handle NsConnRequire() explicitly after
         * Ns_ParseObjv() to be able to provide syntax error messages also in
         * cases, where no connection is required.
         */
        switch (opt) {
        case CClientdataIdx: {
            char       *valueString = NULL;
            Ns_ObjvSpec largs[] = {
                {"?value", Ns_ObjvString,  &valueString, NULL},
                {NULL, NULL, NULL, NULL}
            };

            if (Ns_ParseObjv(NULL, largs, interp, 2, objc, objv) != NS_OK
                || (NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK) ) {
                result = TCL_ERROR;

            } else {
                if (valueString != NULL) {
                    ns_free(connPtr->clientData);
                    connPtr->clientData = ns_strdup(valueString);
                }
                Tcl_SetObjResult(interp, Tcl_NewStringObj(connPtr->clientData, TCL_INDEX_NONE));
            }
            break;
        }
        case CCompressIdx: {
            int               level = -1;
            Ns_ObjvValueRange compressRange = {0, 9};
            Ns_ObjvSpec largs[] = {
                {"?level", Ns_ObjvInt,  &level, &compressRange},
                {NULL, NULL, NULL, NULL}
            };
            if (Ns_ParseObjv(NULL, largs, interp, 2, objc, objv) != NS_OK
                || NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK ) {
                result = TCL_ERROR;

            } else if (level >= 0) {
                Ns_ConnSetCompression(conn, level);
            }
            if (result == TCL_OK) {
                Tcl_SetObjResult(interp,
                                 Tcl_NewIntObj(Ns_ConnGetCompression(conn)));
            }
            break;
        }

        case CContentIdx:
            result = ConnContentObjCmd(clientData, interp, objc, objv, required_flags[opt]);
            break;

        case CContentSentLenIdx: {
            Ns_ObjvValueRange sentRange = {0, LLONG_MAX};
            Tcl_WideInt       sent = -1;
            Ns_ObjvSpec largs[] = {
                {"?bytes", Ns_ObjvWideInt,  &sent, &sentRange},
                {NULL, NULL, NULL, NULL}
            };

            if (Ns_ParseObjv(NULL, largs, interp, 2, objc, objv) != NS_OK
                || NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK ) {
                result = TCL_ERROR;

            } else if (sent != -1) {
                Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)connPtr->nContentSent));

            } else {
                connPtr->nContentSent = (size_t)sent;
            }
            break;
        }

        case CCopyIdx:
            result = ConnCopyObjCmd(clientData, interp, objc, objv, required_flags[opt]);
            break;

        case CEncodingIdx: {
            char       *encodingString = NULL;
            Ns_ObjvSpec largs[] = {
                {"?encoding", Ns_ObjvString, &encodingString, NULL},
                {NULL, NULL, NULL, NULL}
            };
            if (Ns_ParseObjv(NULL, largs, interp, 2, objc, objv) != NS_OK
                || NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK ) {
                result = TCL_ERROR;

            } else {
                if (encodingString != NULL) {
                    encoding = Ns_GetCharsetEncoding(encodingString);
                    if (encoding == NULL) {
                        Ns_TclPrintfResult(interp, "no such encoding: %s", encodingString);
                        result = TCL_ERROR;
                    } else {
                        connPtr->outputEncoding = encoding;
                    }
                }

                if ((result == TCL_OK) && (connPtr->outputEncoding != NULL)) {
                    const char *charset = Ns_GetEncodingCharset(connPtr->outputEncoding);
                    Tcl_SetObjResult(interp, Tcl_NewStringObj(charset, TCL_INDEX_NONE));
                }
            }
            break;
        }

        case CFileHdrIdx: NS_FALL_THROUGH; /* fall through */
        case CFileLenIdx: NS_FALL_THROUGH; /* fall through */
        case CFileOffIdx: {
            char       *fileString = NULL;
            Ns_ObjvSpec largs[] = {
                {"file", Ns_ObjvString, &fileString, NULL},
                {NULL, NULL, NULL, NULL}
            };
            if (Ns_ParseObjv(NULL, largs, interp, 2, objc, objv) != NS_OK
                || NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK ) {
                result = TCL_ERROR;

            } else {
                const Tcl_HashEntry *hPtr;

                hPtr = Tcl_FindHashEntry(&connPtr->files, fileString);
                if (hPtr == NULL) {
                    Ns_TclPrintfResult(interp, "no such file: %s", fileString);
                    result = TCL_ERROR;
                } else {
                    const FormFile *filePtr = Tcl_GetHashValue(hPtr);

                    if (opt == (int)CFileOffIdx) {
                        Tcl_SetObjResult(interp, (filePtr->offObj != NULL) ? filePtr->offObj : Tcl_NewObj());
                    } else if (opt == (int)CFileLenIdx) {
                        Tcl_SetObjResult(interp, (filePtr->sizeObj != NULL) ? filePtr->sizeObj : Tcl_NewObj());
                    } else {
                        Tcl_SetObjResult(interp, (filePtr->hdrObj != NULL) ? filePtr->hdrObj : Tcl_NewObj() );
                    }
                }
            }
            break;
        }

        case CFormIdx: {
            result = ConnFormObjCmd(clientData, interp, objc, objv, required_flags[opt]);
            break;
        }

        case CHostIdx: {
            char       *defaultValue = (char *)NS_EMPTY_STRING;
            Ns_ObjvSpec largs[] = {
                {"?default", Ns_ObjvString, &defaultValue, NULL},
                {NULL, NULL, NULL, NULL}
            };

            if (Ns_ParseObjv(NULL, largs, interp, 2, objc, objv) != NS_OK
                || NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK ) {
                result = TCL_ERROR;

            } else {
                assert(connPtr != NULL);
                request = &connPtr->request;
                assert(request != NULL);

                Tcl_SetObjResult(interp,
                                 Tcl_NewStringObj(request->host == NULL
                                                  ? defaultValue
                                                  : request->host, TCL_INDEX_NONE));
            }
            break;
        }

        case CKeepAliveIdx: {
            int               keepValue = -1;
            Ns_ObjvValueRange keepRange = {0, 1};
            Ns_ObjvSpec largs[] = {
                {"?value", Ns_ObjvInt, &keepValue, &keepRange},
                {NULL, NULL, NULL, NULL}
            };
            if (Ns_ParseObjv(NULL, largs, interp, 2, objc, objv) != NS_OK
                || NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK ) {
                result = TCL_ERROR;

            } else {
                if (keepValue != -1) {
                    connPtr->keep = keepValue;
                }
                Tcl_SetObjResult(interp, Tcl_NewIntObj(connPtr->keep));
            }
            break;
        }

        case CPeerAddrIdx: {
            int                 source = INTCHAR('c');
            static Ns_ObjvTable sourceTable[] = {
                {"configured", UCHAR('c')},
                {"direct",     UCHAR('d')},
                {"forwarded",  UCHAR('f')},
                {NULL,         0u}
            };
            Ns_ObjvSpec lopts[] = {
                {"-source", Ns_ObjvIndex,  &source, sourceTable},
                {NULL, NULL, NULL, NULL}
            };

            if (Ns_ParseObjv(lopts, NULL, interp, 2, objc, objv) != NS_OK
                || NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK ) {
                result = TCL_ERROR;

            } else if (source == INTCHAR('c')) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_ConnConfiguredPeerAddr(conn), TCL_INDEX_NONE));
            } else if (source == INTCHAR('d')) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_ConnPeerAddr(conn), TCL_INDEX_NONE));
            } else {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_ConnForwardedPeerAddr(conn), TCL_INDEX_NONE));
            }
            break;
        }

        case CRatelimitIdx: {
            int         rateLimit = -1;
            Ns_ObjvSpec largs[] = {
                {"?limit", Ns_ObjvInt, &rateLimit, &posintRange0},
                {NULL, NULL, NULL, NULL}
            };

            if (Ns_ParseObjv(NULL, largs, interp, 2, objc, objv) != NS_OK
                || NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK ) {
                result = TCL_ERROR;

            } else {
                if (rateLimit != -1) {
                    connPtr->rateLimit = rateLimit;
                }
                Tcl_SetObjResult(interp, Tcl_NewIntObj(connPtr->rateLimit));
            }
            break;
        }

        case CStatusIdx: {
            int               status = -1;
            Ns_ObjvValueRange statusRange = {100, 599};
            Ns_ObjvSpec       largs[] = {
                {"?status-code", Ns_ObjvInt, &status, &statusRange},
                {NULL, NULL, NULL, NULL}
            };

            if (Ns_ParseObjv(NULL, largs, interp, 2, objc, objv) != NS_OK
                || NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK ) {
                result = TCL_ERROR;

            } else if (status != -1) {
                if (NsConnRequire(interp, NS_CONN_REQUIRE_CONNECTED, &conn, &result) != NS_OK) {
                    result = TCL_ERROR;
                } else {
                    Tcl_SetObjResult(interp, Tcl_NewIntObj(Ns_ConnResponseStatus(conn)));
                    Ns_ConnSetResponseStatus(conn, status);
                }
            }
            if (result == TCL_OK) {
                Tcl_SetObjResult(interp, Tcl_NewIntObj(Ns_ConnResponseStatus(conn)));
            }
            break;
        }

        case CUrlEncodingIdx: {
            char       *encodingString = NULL;
            Ns_ObjvSpec largs[] = {
                {"?encoding", Ns_ObjvString, &encodingString, NULL},
                {NULL, NULL, NULL, NULL}
            };
            if (Ns_ParseObjv(NULL, largs, interp, 2, objc, objv) != NS_OK
                || NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK ) {
                result = TCL_ERROR;

            } else if (encodingString != NULL) {
                encoding = Ns_GetCharsetEncoding(encodingString);
                if (encoding == NULL) {
                    Ns_TclPrintfResult(interp, "no such encoding: %s", encodingString);
                    result = TCL_ERROR;

                } else {
                    /*
                     * Check to see if form data has already been parsed.
                     * If so, and the urlEncoding is changing, then clear
                     * the previous form data.
                     */
                    if ((connPtr->urlEncoding != encoding)
                        && (itPtr->nsconn.flags & CONN_TCLFORM) != 0u) {

                        Ns_ConnClearQuery(conn);
                        itPtr->nsconn.flags ^= CONN_TCLFORM;
                    }
                    connPtr->urlEncoding = encoding;
                }
            }
            if ((result == TCL_OK) && (connPtr->urlEncoding != NULL)) {
                const char *charset = Ns_GetEncodingCharset(connPtr->urlEncoding);
                Tcl_SetObjResult(interp, Tcl_NewStringObj(charset, TCL_INDEX_NONE));
            }
            break;
        }

        case CUrlvIdx: {
            int         idx = -1;
            Ns_ObjvSpec largs[] = {
                {"?idx", Ns_ObjvInt, &idx, &posintRange0},
                {NULL, NULL, NULL, NULL}
            };

            if (Ns_ParseObjv(NULL, largs, interp, 2, objc, objv) != NS_OK
                || NsConnRequire(interp, required_flags[opt], NULL, &result) != NS_OK ) {
                result = TCL_ERROR;

            } else {
                assert(connPtr != NULL);
                request = &connPtr->request;
                assert(request != NULL);

                if (idx != -1) {
                    if (idx > (int)request->urlc - 1) {
                        Ns_TclPrintfResult(interp, "provided index %d exceeds length of urlv", idx);
                        result = TCL_ERROR;
                    } else {
                        const char **elements;
                        TCL_SIZE_T   length;

                        (void)Tcl_SplitList(NULL, request->urlv, &length, &elements);
                        Tcl_SetObjResult(interp, Tcl_NewStringObj(elements[idx], TCL_INDEX_NONE));
                        Tcl_Free((char *) elements);
                    }
                } else {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj(request->urlv, request->urlv_len));
                }
            }
            break;
        }

        default:
            /*
             * All other subcommands receive no arguments.
             */
            result = ConnNoArg(opt, required_flags[opt], connPtr, itPtr, objc, objv);
        }
    }
    return result;
}

#if 0
static struct sockaddr *
ConnGetSockAddr(const Ns_Conn *conn, struct sockaddr *saPtr)
{
    Ns_Sock *sockPtr = Ns_ConnSockPtr(conn);
    struct sockaddr *sockSaPtr = NULL;

    if (sockSaPtr != NULL) {
        sockSaPtr = Ns_SockGetClientSockAddr(sockPtr);
    }
    if (saPtr == NULL || saPtr->sa_family == 0) {
        const char* peerAddrString = Ns_ConnConfiguredPeerAddr(conn);

        ns_inet_pton(saPtr, peerAddrString);
    } else {
        saPtr = sockSaPtr;
    }
    return saPtr;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * ConnNoArg --
 *
 *      Helper function of NsTclConnObjCmd. All of these subcommands of
 *      "ns_conn" receive no arguments. When adding further arguments, the
 *      subcommand must be moved to the calling witch statement.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
ConnNoArg(int opt, unsigned int required_flags, Conn *connPtr, NsInterp *itPtr, TCL_SIZE_T objc, Tcl_Obj *const* objv)
{
    int                  result = TCL_OK;
    TCL_SIZE_T           setNameLength;
    const char          *setName;
    const Ns_Request    *request = NULL;
    Ns_Conn             *conn = (Ns_Conn *)connPtr;
    Tcl_Interp          *interp = itPtr->interp;

    if (Ns_ParseObjv(NULL, NULL, interp, 2, objc, objv) != NS_OK) {
        result = TCL_ERROR;
    } else {
        if (required_flags != 0u) {
            /*
             * We have to check the connection requirements.
             */
            if (NsConnRequire(interp, required_flags, NULL, &result) == NS_OK) {
                /*
                 * We know that connPtr can't be NULL.
                 */
                assert(connPtr != NULL);
                request = &connPtr->request;
            } else {
                result = TCL_ERROR;
            }
        } else {
            request = connPtr != NULL ? &connPtr->request : NULL;
        }
    }
    if (result == TCL_ERROR) {
        return result;
    }

    /*
     * Each time, when NsConnRequire was called and succeeded, the "request"
     * pointer must be not NULL.
     */
    if (required_flags != 0u) {
        assert(request != NULL);
    }

    switch (opt) {
    case CIsConnectedIdx:
        /*
         * We report true, when we have a connection and the connection is not
         * closed.
         */
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj((connPtr != NULL)
                                                   ? ((connPtr->flags & NS_CONN_CLOSED) == 0u)
                                                   : NS_FALSE));
        break;

    case CCurrentAddrIdx:
        {
            const char *addr = Ns_ConnCurrentAddr(conn);

            Tcl_SetObjResult(interp, Tcl_NewStringObj((addr != NULL) ? addr : NS_EMPTY_STRING, TCL_INDEX_NONE));
        }
        break;

    case CCurrentPortIdx:
        {
            unsigned short port = Ns_ConnCurrentPort(conn);

            Tcl_SetObjResult(interp, Tcl_NewIntObj((int)port));
        }
        break;

    case CAuthIdx:
        if ((itPtr->nsconn.flags & CONN_TCLAUTH) != 0u) {
            Tcl_SetResult(interp, itPtr->nsconn.auth, TCL_STATIC);
        } else {
            if (connPtr->auth == NULL) {
                connPtr->auth = Ns_SetCreate(NS_SET_NAME_AUTH);
            }
            if (unlikely(Ns_TclEnterSet(interp, connPtr->auth, NS_TCL_SET_STATIC) != TCL_OK)) {
                result = TCL_ERROR;
            } else {
                setName = Tcl_GetStringFromObj(Tcl_GetObjResult(interp), &setNameLength);
                setNameLength++;
                memcpy(itPtr->nsconn.auth, setName, MIN((size_t)setNameLength, NS_SET_SIZE));
                itPtr->nsconn.flags |= CONN_TCLAUTH;
            }
        }
        break;

    case CAuthUserIdx:
        if (connPtr->auth != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_ConnAuthUser(conn), TCL_INDEX_NONE));
        }
        break;

    case CAuthPasswordIdx:
        if (connPtr->auth != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_ConnAuthPasswd(conn), TCL_INDEX_NONE));
        }
        break;

    case CContentLengthIdx:
        Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)conn->contentLength));
        break;

    case CContentFileIdx:
        {
            const char *file = Ns_ConnContentFile(conn);
            if (file != NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(file, TCL_INDEX_NONE));
            }
        }
        break;

    case CPeerPortIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj((int)Ns_ConnPeerPort(conn)));
        break;

    case CHeaderLengthIdx:
        Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)connPtr->reqPtr->coff));
        break;

    case CHeadersIdx:
        if (likely((itPtr->nsconn.flags & CONN_TCLHDRS) != 0u)) {
            Tcl_SetResult(interp, itPtr->nsconn.hdrs, TCL_STATIC);
        } else {
            if (unlikely(Ns_TclEnterSet(interp, connPtr->headers, NS_TCL_SET_STATIC) != TCL_OK)) {
                result = TCL_ERROR;
            } else {
                setName = Tcl_GetStringFromObj(Tcl_GetObjResult(interp), &setNameLength);
                setNameLength++;
                memcpy(itPtr->nsconn.hdrs, setName, MIN((size_t)setNameLength, NS_SET_SIZE));
                itPtr->nsconn.flags |= CONN_TCLHDRS;
            }
        }
        break;

    case COutputHeadersIdx:
        if (likely((itPtr->nsconn.flags & CONN_TCLOUTHDRS) != 0u)) {
            Tcl_SetResult(interp, itPtr->nsconn.outhdrs, TCL_STATIC);
        } else {
            if (unlikely(Ns_TclEnterSet(interp, connPtr->outputheaders, NS_TCL_SET_STATIC) != TCL_OK)) {
                result = TCL_ERROR;
            } else {
                setName = Tcl_GetStringFromObj(Tcl_GetObjResult(interp), &setNameLength);
                setNameLength++;
                memcpy(itPtr->nsconn.outhdrs, setName, MIN((size_t)setNameLength, NS_SET_SIZE));
                itPtr->nsconn.flags |= CONN_TCLOUTHDRS;
            }
        }
        break;

    case CFilesIdx:
        {
            const Tcl_HashEntry *hPtr;
            Tcl_HashSearch       search;
            Tcl_Obj             *listObj = Tcl_NewListObj(0, NULL);

            for (hPtr = Tcl_FirstHashEntry(&connPtr->files, &search);
                 hPtr != NULL;
                 hPtr = Tcl_NextHashEntry(&search)
                 ) {
                const char *key = Tcl_GetHashKey(&connPtr->files, hPtr);
                Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(key, TCL_INDEX_NONE));
            }
            Tcl_SetObjResult(interp, listObj);
        }
        break;

    case CRequestIdx:
        assert(request != NULL);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(request->line, TCL_INDEX_NONE));
        break;

    case CMethodIdx:
        assert(request != NULL);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(request->method, TCL_INDEX_NONE));
        break;

    case CPartialTimesIdx:
        {
            Ns_Time   now, acceptTime, queueTime, filterTime, runTime;
            Tcl_DString ds, *dsPtr = &ds;

            Ns_GetTime(&now);
            Tcl_DStringInit(dsPtr);

            (void)Ns_DiffTime(&connPtr->requestQueueTime,   &connPtr->acceptTime,         &acceptTime);
            (void)Ns_DiffTime(&connPtr->requestDequeueTime, &connPtr->requestQueueTime,   &queueTime);
            (void)Ns_DiffTime(&connPtr->filterDoneTime,     &connPtr->requestDequeueTime, &filterTime);
            (void)Ns_DiffTime(&now,                         &connPtr->filterDoneTime,     &runTime);

            Tcl_DStringAppend(dsPtr, "accepttime ", 11);
            Ns_DStringAppendTime(dsPtr, &acceptTime);

            Tcl_DStringAppend(dsPtr, " queuetime ", 11);
            Ns_DStringAppendTime(dsPtr, &queueTime);

            Tcl_DStringAppend(dsPtr, " filtertime ", 12);
            Ns_DStringAppendTime(dsPtr, &filterTime);

            Tcl_DStringAppend(dsPtr, " runtime ", 9);
            Ns_DStringAppendTime(dsPtr, &runTime);

            Tcl_DStringResult(interp, dsPtr);

            break;
        }

    case CProtocolIdx:
        assert(request != NULL);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(request->requestType == NS_REQUEST_TYPE_PROXY
                                                  ? request->protocol
                                                  : connPtr->drvPtr->protocol, TCL_INDEX_NONE));
        break;


    case CPortIdx:
        assert(request != NULL);
        Tcl_SetObjResult(interp, Tcl_NewIntObj((int)request->port));
        break;

    case CUrlIdx:
        assert(request != NULL);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(request->url, request->url_len));
        break;

    case CQueryIdx:
        assert(request != NULL);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(request->query, TCL_INDEX_NONE));
        break;

    case CFragmentIdx:
        assert(request != NULL);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(request->fragment, TCL_INDEX_NONE));
        break;

    case CUrlcIdx:
        assert(request != NULL);
        Tcl_SetObjResult(interp, Tcl_NewIntObj(request->urlc));
        break;

    case CVersionIdx:
        assert(request != NULL);
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(request->version));
        break;

    case CLocationIdx:
        {
            Tcl_DString ds;

            Tcl_DStringInit(&ds);
            (void) Ns_ConnLocationAppend(conn, &ds);
            Tcl_DStringResult(interp, &ds);
            break;
        }

    case CDriverIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_ConnDriverName(conn), TCL_INDEX_NONE));
        break;

    case CDetailsIdx: {
        Tcl_Obj     *dictObj;
        const char  *currentAddr = Ns_ConnCurrentAddr(conn);
        Tcl_DString  ds;

        dictObj = (connPtr->drvPtr->connInfoProc != NULL)
            ? connPtr->drvPtr->connInfoProc(Ns_ConnSockPtr(conn))
            : Tcl_NewDictObj();

        Tcl_DictObjPut(NULL, dictObj,
                       Tcl_NewStringObj("proxied", 7),
                       Tcl_NewBooleanObj(nsconf.reverseproxymode.enabled));

        Tcl_DictObjPut(NULL, dictObj,
                       Tcl_NewStringObj("currentaddr", 11),
                       Tcl_NewStringObj(currentAddr != NULL ? currentAddr : "na", -1));

        if (currentAddr != NULL) {
            struct NS_SOCKADDR_STORAGE sa;
            struct sockaddr *saPtr = (struct sockaddr*)&sa;

            ns_inet_pton(saPtr, currentAddr);
            (void)Ns_SockaddrAddToDictIpProperties(saPtr, dictObj);
        }

        Tcl_DStringInit(&ds);
        DStringAppendConnFlags(&ds, connPtr->flags);
        Tcl_DictObjPut(NULL, dictObj,
                       Tcl_NewStringObj("flags", 5),
                       Tcl_NewStringObj(ds.string, ds.length));
        Tcl_DStringFree(&ds);

        Tcl_SetObjResult(interp, dictObj);
        break;
    }

    case CServerIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_ConnServer(conn), TCL_INDEX_NONE));
        break;

    case CPoolIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(connPtr->poolPtr->pool, TCL_INDEX_NONE));
        break;

    case CTargetIdx: {
        Tcl_DString lineDs;

        Tcl_DStringInit(&lineDs);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_ConnTarget(conn, &lineDs), TCL_INDEX_NONE));
        Tcl_DStringFree(&lineDs);
        break;
    }
    case CTimeoutIdx:
        Tcl_SetObjResult(interp, Ns_TclNewTimeObj(Ns_ConnTimeout(conn)));
        break;

    case CSockIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj((int)Ns_ConnSock(conn)));
        break;

    case CIdIdx:
        Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)Ns_ConnId(conn)));
        break;

    case CFlagsIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj((int)connPtr->flags));
        break;

    case CStartIdx:
        Tcl_SetObjResult(interp, Ns_TclNewTimeObj(&connPtr->requestQueueTime));
        break;

    case CCloseIdx:
        (void) Ns_ConnClose(conn);
        break;

    case CChannelIdx: {
        Tcl_Channel chan = MakeConnChannel(itPtr, conn);
        if (chan == NULL) {
            result = TCL_ERROR;
        } else {
            Tcl_RegisterChannel(interp, chan);
            Tcl_SetObjResult(interp, Tcl_NewStringObj(Tcl_GetChannelName(chan), TCL_INDEX_NONE));
        }
        break;
    }

    case CUrlDictIdx: {
        Ns_URL      u;
        char       *requestLine = ns_strdup(request->line), *urlString;
        const char *errMsg = NULL;

        assert(request != NULL);
        urlString = strchr(requestLine, INTCHAR(' '));
        if (urlString == NULL) {
            errMsg = "no space";
        } else {
            char *version = strrchr(urlString, INTCHAR(' '));

            if (version != NULL) {
                *version = '\0';
            }
            Ns_ParseUrl(urlString +1, NS_FALSE, &u, &errMsg);
        }
        if (errMsg != NULL) {
            Ns_TclPrintfResult(interp, "Could not parse URL \"%s\": %s", urlString, errMsg);
        } else {
            Tcl_SetObjResult(interp, NsUrlToDictObj(interp, &u));
        }
        ns_free(requestLine);
        break;
    }

    case CZipacceptedIdx:
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj((connPtr->flags & NS_CONN_ZIPACCEPTED) != 0u));
        break;

    case CAacceptedcompressionIdx:
        {
            Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);

            if ((connPtr->flags & NS_CONN_BROTLIACCEPTED) != 0u) {
                Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj("brotli", 6));
            }
            if ((connPtr->flags & NS_CONN_ZIPACCEPTED) != 0u) {
                Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj("gzip", 4));
            }

            Tcl_SetObjResult(interp, listObj);
        }
        break;

    default:
        /* unexpected value */
        fprintf(stderr, "OPT %d <%s>\n", opt, Tcl_GetString(objv[1]));
        assert(opt && 0);
        break;

    }

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLocationProcObjCmd --
 *
 *      Implements "ns_locationproc".
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclLocationProcObjCmd(ClientData UNUSED(clientData), Tcl_Interp *interp, TCL_SIZE_T objc, Tcl_Obj *const* objv)
{
    const NsServer *servPtr = NsGetInitServer();
    int             result = TCL_OK;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "/script/ ?/arg .../?");
        result = TCL_ERROR;

    } else if (servPtr == NULL) {
        Ns_TclPrintfResult(interp, "no initializing server");
        result = TCL_ERROR;
    } else {
        Ns_TclCallback *cbPtr = Ns_TclNewCallback(interp, (ns_funcptr_t)NsTclConnLocation,
                                                  objv[1], (TCL_SIZE_T)(objc - 2), objv + 2);
        if (Ns_SetConnLocationProc(NsTclConnLocation, cbPtr) != NS_OK) {
            result = TCL_ERROR;
        }
    }

    return result;
}

#ifdef NS_WITH_DEPRECATED
/*
 *----------------------------------------------------------------------
 *
 * NsTclWriteContentObjCmd --
 *
 *      Implements "ns_conncptofp".
 *      Implements "ns_writecontent".
 *
 *      Both commands are deprecated.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclWriteContentObjCmd(ClientData clientData, Tcl_Interp *interp, TCL_SIZE_T objc, Tcl_Obj *const* objv)
{
    const NsInterp   *itPtr = clientData;
    int               result = TCL_OK;
    Tcl_WideInt       toCopy = 0;
    char             *chanName;
    Tcl_Channel       chan;

    /*
     * Syntax: ns_conncptofp ?-bytes tocopy? channel
     */

    Ns_ObjvSpec opts[] = {
        {"-bytes",   Ns_ObjvWideInt, &toCopy, &posSizeRange0},
        {"--",       Ns_ObjvBreak,    NULL,    NULL},
        {NULL,       NULL,            NULL,    NULL}
    };
    Ns_ObjvSpec args[] = {
        {"channel",  Ns_ObjvString, &chanName, NULL},
        {NULL,       NULL,          NULL,      NULL}
    };

    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        result = TCL_ERROR;

    } else if (NsConnRequire(interp, NS_CONN_REQUIRE_ALL, NULL, &result) != NS_OK) {

    } else if (GetChan(interp, chanName, &chan) != TCL_OK) {
        result = TCL_ERROR;

    } else if (Tcl_Flush(chan) != TCL_OK) {
        const char *errorMsg = Tcl_ErrnoMsg(Tcl_GetErrno());

        Ns_TclPrintfResult(interp, "flush returned error: %s", errorMsg);
        Tcl_SetErrorCode(interp, "POSIX", Tcl_ErrnoId(), errorMsg, NS_SENTINEL);
        result = TCL_ERROR;

    } else {
        const Request *reqPtr = ((Conn *)itPtr->conn)->reqPtr;

        Ns_LogDeprecated(objv, 1, "ns_conn copy ...", NULL);

        if ((toCopy > (TCL_SIZE_T)reqPtr->avail) || (toCopy == 0)) {
            toCopy = (TCL_SIZE_T)reqPtr->avail;
        }
        if (Ns_ConnCopyToChannel(itPtr->conn, (size_t)toCopy, chan) != NS_OK) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("could not copy content", TCL_INDEX_NONE));
            result = TCL_ERROR;
        }
    }

    return result;
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * NsTclConnLocation --
 *
 *      Tcl callback to construct location string.
 *
 * Results:
 *      dest->string or NULL on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
NsTclConnLocation(Ns_Conn *conn, Tcl_DString *dest, const Ns_TclCallback *cbPtr)
{
    Tcl_Interp *interp = Ns_GetConnInterp(conn);
    char       *result;

    if (Ns_TclEvalCallback(interp, cbPtr, dest, NS_SENTINEL) != TCL_OK) {
        (void) Ns_TclLogErrorInfo(interp, "\n(context: location callback)");
        result =  NULL;
    } else {
        result = dest->string;
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * GetChan --
 *
 *      Return an open channel.
 *
 * Results:
 *      TCL_OK if given a valid channel id, TCL_ERROR otherwise.
 *
 * Side effects:
 *      Channel is set in given chanPtr or error message left in
 *      given interp.
 *
 *----------------------------------------------------------------------
 */

static int
GetChan(Tcl_Interp *interp, const char *id, Tcl_Channel *chanPtr)
{
    Tcl_Channel chan;
    int         mode, result = TCL_OK;

    NS_NONNULL_ASSERT(interp != NULL);
    NS_NONNULL_ASSERT(id != NULL);
    NS_NONNULL_ASSERT(chanPtr != NULL);

    chan = Tcl_GetChannel(interp, id, &mode);
    if (chan == (Tcl_Channel) NULL) {
        result = TCL_ERROR;

    } else if ((mode & TCL_WRITABLE) == 0) {
        Ns_TclPrintfResult(interp, "channel \"%s\" wasn't opened for writing", id);
        result = TCL_ERROR;

    } else {
        *chanPtr = chan;
    }

    return result;
}


/*----------------------------------------------------------------------------
 *
 * MakeConnChannel --
 *
 *      Wraps a Tcl channel around the current connection socket
 *      and returns the channel handle to the caller.
 *
 * Result:
 *      Tcl_Channel handle or NULL.
 *
 * Side Effects:
 *      Removes the socket from the connection structure.
 *
 *----------------------------------------------------------------------------
 */

static Tcl_Channel
MakeConnChannel(const NsInterp *itPtr, Ns_Conn *conn)
{
    Conn       *connPtr;
    Tcl_Channel chan = NULL;

    NS_NONNULL_ASSERT(conn != NULL);
    NS_NONNULL_ASSERT(itPtr != NULL);

    connPtr = (Conn *) conn;
    if (unlikely((connPtr->flags & NS_CONN_CLOSED) != 0u)) {
        Ns_TclPrintfResult(itPtr->interp, "connection closed");

    } else {

        assert(connPtr->sockPtr != NULL);
        if (connPtr->sockPtr->sock == NS_INVALID_SOCKET) {
            Ns_TclPrintfResult(itPtr->interp, "no socket for connection");

        } else {

            /*
             * Create Tcl channel around the connection socket
             */

            chan = Tcl_MakeTcpClientChannel(NSSOCK2PTR(connPtr->sockPtr->sock));
            if (unlikely(chan == NULL)) {
                Ns_TclPrintfResult(itPtr->interp, "%s", Tcl_PosixError(itPtr->interp));

            } else {
                /*
                 * Disable keep-alive and chunking headers.
                 */

                if (connPtr->responseLength < 0) {
                    connPtr->keep = (int)NS_FALSE;
                }

                /*
                 * Check to see if HTTP headers are required and flush
                 * them now before the conn socket is dissociated.
                 */

                if ((conn->flags & NS_CONN_SENTHDRS) == 0u) {
                    if ((itPtr->nsconn.flags & CONN_TCLHTTP) == 0u) {
                        conn->flags |= NS_CONN_SKIPHDRS;
                    } else {
                        if (Ns_ConnWriteVData(conn, NULL, 0, NS_CONN_STREAM) != NS_OK) {
                            Ns_Log(Error, "make channel: error writing headers");
                        }
                    }
                }

                if (Ns_SockSetBlocking(connPtr->sockPtr->sock) != NS_OK) {
                    Ns_Log(Error, "make channel: error while making channel blocking");
                }

                connPtr->sockPtr->sock = NS_INVALID_SOCKET;
            }
        }
    }

    return chan;
}

/*
 *----------------------------------------------------------------------
 *
 * NsConnRequire --
 *
 *      Return the conn for the given interp, in case it is fully functioning.
 *      In case that interp is
 *
 *      - not connected at all (e.g. no connection thread), or
 *      - when the sockPtr of the connection was detached, or
 *      - when the connection is already closed,
 *
 *      return NS_ERROR and set an appropriate error message when
 *      rejectalreadyclosedconn is true (default). When this parameter is set
 *      to false, it causes a soft error and returns the Tcl status code as
 *      last argument.
 *
 *      If the connection is valid, the function return NS_OK and returns the connPtr
 *      in its third argument.
 *
 * Results:
 *      NaviServer result code.
 *
 * Side effects:
 *      Sets Tcl result on error.
 *
 *----------------------------------------------------------------------
 */

Ns_ReturnCode
NsConnRequire(Tcl_Interp *interp, unsigned int flags, Ns_Conn **connPtr, int *tclResultPtr)
{
    Ns_Conn      *conn;
    Ns_ReturnCode status;
    bool          softError = NS_FALSE;

    NS_NONNULL_ASSERT(interp != NULL);

    conn = Ns_TclGetConn(interp);
    if (conn == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no connection", TCL_INDEX_NONE));
        status = NS_ERROR;

    } else if (((flags & NS_CONN_REQUIRE_CONNECTED) != 0u)
               && (Ns_ConnSockPtr(conn) == NULL)) {
        softError = (!nsconf.reject_already_closed_or_detached_connection);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("connection socket is detached", TCL_INDEX_NONE));
        status = NS_ERROR;

    } else if (((flags & NS_CONN_REQUIRE_OPEN) != 0u)
               && ((conn->flags & NS_CONN_CLOSED) != 0u)) {
        softError = (!nsconf.reject_already_closed_or_detached_connection);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("connection already closed", TCL_INDEX_NONE));
        status = NS_ERROR;

    } else if (((flags & NS_CONN_REQUIRE_CONFIGURED) != 0u)
               && ((conn->flags & NS_CONN_CONFIGURED) == 0u)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("connection is not configured", TCL_INDEX_NONE));
        status = NS_ERROR;

    } else {
        if (connPtr != NULL) {
            *connPtr = conn;
        }
        status = NS_OK;
    }

    if (tclResultPtr != NULL) {
        *tclResultPtr = TCL_OK;
        if (status == NS_ERROR) {
            if (softError) {
                Tcl_ResetResult(interp);
                Ns_Log(Notice, "skip output due to rejectalreadyclosedconn == false");
            } else {
                *tclResultPtr = TCL_ERROR;
            }
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnGetUrlSpaceMatchInfo --
 *
 *      Accessor function to return a pointer to the UrlSpaceMatchInfo member.
 *      If the matchInfo is not set, the members of are matchInfo are NULL.
 *
 * Results:
 *      Pointer to member.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
const Ns_UrlSpaceMatchInfo *
Ns_ConnGetUrlSpaceMatchInfo(const Ns_Conn *conn)
{
    NS_NONNULL_ASSERT(conn != NULL);

    return &((const Conn *) conn)->matchInfo;
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */
