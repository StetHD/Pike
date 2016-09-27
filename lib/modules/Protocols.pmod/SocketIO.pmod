/*
 * Clean-room Socket.IO implementation for Pike.
 */

//! This is an implementation of the Socket.IO server-side communication's
//! driver.  It basically is a real-time bidirectional object-oriented
//! communication's protocol for communicating between a webbrowser
//! and a server.
//!
//! This module supports the following features:
//! @ul
//! @item Passing any arbitrarily deep nested data object
//!  which can be represented in basic JavaScript datatypes.
//! @item In addition to the normal JavaScript datatypes, it also
//!  supports passing (nested) binary blobs in an efficient manner.
//! @item Every message/event sent, allows for a callback acknowledge
//!  to be specified.
//! @item Acknowledge callbacks which will be called when the other side
//!  actively decides to acknowledge it (not automatically upon
//!  message reception).
//! @item Acknowledgement messages can carry an arbitrary amount of
//!  data.
//! @endul
//!
//! The driver uses @[Protocol.EngineIO] as the underlying protocol.
//!
//! @seealso
//!  @[Protocol.EngineIO], @[Protocol.WebSocket],
//!  @url{http://github.com/socketio/socket.io-protocol@},
//!  @url{http://socket.io/@}

#pike __REAL_VERSION__

//#define SIO_DEBUG		1
//#define SIO_DEBUGMORE		1

//#define SIO_STATS		1	// Collect extra usage statistics

#define DRIVERNAME		"Socket.IO"

#define DERROR(msg ...)		({sprintf(msg),backtrace()})
#define SERROR(msg ...)		(sprintf(msg))
#define USERERROR(msg)		throw(msg)
#define SUSERERROR(msg ...)	USERERROR(SERROR(msg))
#define DUSERERROR(msg ...)	USERERROR(DERROR(msg))

#ifdef SIO_DEBUG
#define PD(X ...)		werror(X)
#define PDT(X ...)		(werror(X), \
				 werror(describe_backtrace(PT(backtrace()))))
				// PT() puts this in the backtrace
#define PT(X ...)		(lambda(object _this){return (X);}(this))
#else
#undef SIO_DEBUGMORE
#define PD(X ...)		0
#define PDT(X ...)		0
#define PT(X ...)		(X)
#endif

//! Global options for all SocketIO instances.
//!
//! @seealso
//!  @[SocketIO.farm()]
final mapping options = ([
]);

constant protocol = 4;		// SIO Protocol version

private enum {
  //    0      1           2      3    4      5             6
  CONNECT='0', DISCONNECT, EVENT, ACK, ERROR, BINARY_EVENT, BINARY_ACK
};

private array emptyarray = ({});

class Universe {
  // All Socket.IO nsps with connected clients in this universe.
  private mapping(string:
   mapping(string|int:function(mixed, string, mixed ...:mixed)))
   nsps = ([]);

  //! All SocketIO sessions in this universe.
  final multiset(Client) clients = (<>);

  //! @param _options
  //! Optional options to override the defaults.
  //! This parameter is passed down as is to the underlying
  //!  @[EngineIO.Socket].
  //!
  //! @example
  //! Sample minimal implementation of a SocketIO server farm:
  //!
  //!Protocols.SocketIO.Universe myuniverse;
  //!
  //!mixed echo(mixed id, string namespace, mixed ... data) {
  //!  id->write(data);
  //!  if (sendack)
  //!    sendack("Ack","me","harder");
  //!}
  //!
  //!void wsrequest(array(string) protocols, object req) {
  //!  httprequest(req);
  //!}
  //!
  //!void httprequest(object req) {
  //!  switch (req.not_query) {
  //!    case "/socket.io/":
  //!      Protocols.SocketIO.Client client = myuniverse.farm(req);
  //!      if (client)
  //!        client->write("Hello world!");
  //!      break;
  //!  }
  //!}
  //!
  //!int main(int argc, array(string) argv) {
  //!  myuniverse = Protocols.SocketIO.Universe(); // Create universe
  //!  myuniverse.register("", echo); // Register root namespace
  //!  Protocols.WebSocket.Port(httprequest, wsrequest, 80);
  //!  return -1;
  //!}
  //!
  //! @seealso
  //!  @[EngineIO.farm()]
  final Client farm(Protocols.WebSocket.Request req, void|mapping _options) {
    if (_options)
      _options = options + _options;
    else
      _options = options;
    Protocols.EngineIO.Socket con = Protocols.EngineIO.farm(req, _options);
    return con && Client(this, con);
  }

  private void low_register(string namespace, void|mixed event,
   void|function(mixed, string, mixed ...:mixed) fn) {
    mapping nsp = nsps[namespace];
    if (!nsp)
      nsps[namespace] = nsp = ([]);
    else if(fn)
      nsp[event] = fn;
    else {
      m_delete(nsp, event);
      if (!sizeof(nsp))
        m_delete(nsps, namespace);
    }
  }

  //! Register worker callback on namespace and event.
  //! There can be only one worker per tuple.
  //! Workers can be unregistered by omitting @expr{fn@}.
  //! Having a default worker per namespace in addition zero or more
  //! event specific ones, is supported.
  final variant void register(string namespace, string event,
   void|function(mixed, string, string, mixed ...:mixed) fn) {
    low_register(namespace, event, fn);
  }
  final variant void register(string namespace,
   void|function(mixed, string, mixed ...:mixed) fn) {
    low_register(namespace, 0, fn);
  }

  private string invalidnamespace = "Invalid namespace";

  //! Connects and disconnects clients.
  //!
  //! @returns
  //!  0 upon success, the error itself otherwise.
  final mixed
   connect(Client client, string oldnamespace, void|string newnamespace) {
    clients[client] = !!newnamespace;
    return nsps[newnamespace] ? 0 : invalidnamespace;
  }

  //!
  final mixed read(string namespace, mixed id,
   function(mixed ...:void) sendackcb, mixed ... data) {
    function(mixed, string, mixed ...:mixed) fn;
    mapping nsp = nsps[namespace];
    if (!nsp)
      return "\"" + invalidnamespace + "\"";
    if (!(fn = nsp[sizeof(data) && data[0]] || nsp[0]))
      return (sizeof(data) ? Standards.JSON.encode(data[0]) : "")
       + ",\"Unregistered event\"";
    mixed res = fn(id, namespace, @data);
    if (sendackcb)
      if (arrayp(res))
        sendackcb(@res);
      else
        sendackcb(res);
    return 0;
  }
}

//! Runs a single Socket.IO session.
class Client {
  private Universe universe;

  //!
  string namespace="";

  private mixed id;
  private enum {RUNNING = 0, SDISCONNECT, RDISCONNECT};
  private int state = RUNNING;
  private array curevent;
  private array curbins;
  private int ackid = 0, bins = 0, curackid, curtype;
  // In the upstream version conn is publicly accessible.
  // Until proven otherwise, this sounds like a bad idea.
  private .EngineIO.Socket conn;
  private mapping(int:function(mixed, mixed ...:void)) ack_cbs = ([]);

  //! Set initial argument on callbacks.
  final void set_id(mixed _id) {
    id = _id;
  }

  //! Retrieve initial argument on callbacks.
  final mixed query_id() {
    return id || this;
  }

  //! This session's unique session id.
  final string `sid() {
    return conn.sid;
  }

  //! Contains the last request seen on this connection.
  //! Can be used to obtain cookies etc.
  final Protocols.WebSocket.Request `request() {
    return conn.request;
  }

  //! Send text or binary messages.
  final void write(mixed|function(mixed, mixed ...:void) ack_cb,
   mixed ... data) {
    if (state >= SDISCONNECT)
      DUSERERROR("Socket already shutting down");
    if (functionp(ack_cb))
      send(EVENT, data, ack_cb);
    else
      send(EVENT, ({ack_cb}) + data);
  }

  //! Send text or binary events.
  final variant void emit(function(mixed, mixed ...:void) ack_cb,
   string event, mixed ... data) {
    write(ack_cb, event, @data);
  }
  final variant void emit(string event, mixed ... data) {
    write(event, @data);
  }

  private void send(int type, void|string|array data,
   void|int|function(mixed, mixed ...:void) ack_cb) {
    PD("Send %s %d %c:%O\n", sid, intp(ack_cb)?ack_cb:-1, type, data);
    array sbins = emptyarray;
    int cackid;

    void treeexport(mixed level) {
      foreach(level; mixed i; mixed v)
        if (arrayp(v) || mappingp(v))
          treeexport(v);
        else if(objectp(v)) {
          level[i] = (["_placeholder":1, "num":sizeof(sbins)]);
          sbins += ({v});				// Save binary blobs
        }
    };

    if (arrayp(data)) {
      treeexport(data);
      data = Standards.JSON.encode(data);
    }
    if (!zero_type(ack_cb)) {
      if (type == ACK)
        cackid = ack_cb;
      else				// Save ackid, to make it threadsafe
        ack_cbs[cackid = ackid++] = ack_cb;
    }
    if (sizeof(sbins)) {
      switch(type) {
        case EVENT: type = BINARY_EVENT; break;
        case ACK: type = BINARY_ACK; break;
      }
      if (zero_type(ack_cb))
        data = sprintf("%c%d-%s", type, sizeof(sbins), data);
      else
        data = sprintf("%c%d-%d%s", type, sizeof(sbins), cackid, data);
      conn.write(@(({data}) + sbins));
    } else {
      if (!data)
        data = "";
      if (zero_type(ack_cb))
        data = sprintf("%c%s", type, data);
      else
        data = sprintf("%c%d%s", type, cackid, data);
      conn.write(data);
    }
  }

  //! Close the socket signalling the other side.
  final void close() {
    if (state < SDISCONNECT) {
      PD("Send disconnect, %s state %O\n", sid, state);
      state = SDISCONNECT;
      send(DISCONNECT);
      conn.close();
    }
  }

  private void treeimport(mixed level) {
    foreach(level; mixed i; mixed v) {
      if (arrayp(v))
        ;
      else if (mappingp(v)) {
        if (v->_placeholder) {
          level[i] = curbins[v->num];			// Fill in binary blobs
          continue;
        }
      } else
        continue;
      treeimport(v);
    }
  }

  private void recvcb() {
    void readthread(string namespace, int cackid, mixed ... data) {
      void sendackcb(mixed ... data) {		// saved callback-stackframe
        PD("Ack %d %O\n", cackid, data);
        send(ACK, data, cackid);
      };
      string err = universe ? universe.read(namespace, query_id(),
       cackid >= 0 && sendackcb, @data) : "\"Unknown universe\"";
      if (err)
        send(ERROR, namespace + "," + err);
    };
    switch(curtype) {
      default: {
#if constant(Thread.Thread)
        // Spawn one thread per message/callback.
        // FIXME Too much overhead?
        Thread.Thread(readthread, namespace, curackid, @curevent);
#else
        readthread(namespace, curackid, @curevent);
#endif
        break;
      }
      case ACK:
      case BINARY_ACK: {
        function(mixed, mixed ...:void) ackfn;
        if(ackfn = ack_cbs[curackid]) {
          m_delete(ack_cbs, curackid);		// ACK callbacks can only be
          ackfn(id, @curevent);			// executed once
        }
        break;
      }
    }
  }

  private void closedown() {
    if (state < RDISCONNECT) {
      close();
      state = RDISCONNECT;
      universe.connect(this, namespace);
    }
  }

  private void recv(mixed eid, string|Stdio.Buffer data) {
    PD("Recv %s %O\n", sid, (string)data);
    if (!stringp(data)) {
      curbins[-bins] = data;
      if (!--bins) {
        treeimport(curevent);
        recvcb();
      }
    } else {
      curtype = data[0];
      data = data[1..];
      switch (curtype) {
        default:	  // Protocol error
          send(ERROR, sprintf("%c,\"Invalid packet type\"", curtype));
          break;
        case ERROR:
          SUSERERROR(data);				// Pass error up
        case CONNECT:
          if (universe) {
            mixed e;
            if (e = universe.connect(this, namespace, data)) {
              send(ERROR, data + "," + Standards.JSON.encode(e));
              break;
            }
            state = RUNNING;
            send(CONNECT, namespace = data);		// Confirm namespace
          } else
            send(ERROR, data + ",\"No universe to register to\"");
          break;
        case DISCONNECT:
          closedown();
          universe = 0;
          id = 0;			// Delete all references to this Client
          break;
        case EVENT:
        case ACK:
        case BINARY_EVENT:
        case BINARY_ACK: {
          curevent = array_sscanf(data, "%[0-9]%[-]%[0-9]");
          string s;
          int i = 0;
          curackid = -1;
          if (sizeof(s = curevent[2]))
            curackid = (int)s;
          if (sizeof(s = curevent[0]))
            if (sizeof(curevent[1]))
              bins = (int)s;
            else
              curackid = (int)s;
          foreach(curevent;; string s)
            i += sizeof(s);
          curevent = Standards.JSON.decode(data[i..]);
          curbins = allocate(bins);
          PD("Incoming %c %d %d %O\n", curtype, bins, curackid, curevent);
          switch (curtype) {
            case EVENT:
            case ACK:
              recvcb();
          }
        }
      }
    }
  }

  protected void destroy() {
    closedown();
  }

  protected void create(Universe _universe, Protocols.EngineIO.Socket _con) {
    universe = _universe;
    conn = _con;
    conn.set_callbacks(recv, closedown);
    send(CONNECT);			// Autconnect to root namespace
    PD("New SocketIO sid: %O\n", sid);
  }

  private string _sprintf(int type, void|mapping flags) {
    string res=UNDEFINED;
    switch (type) {
      case 'O':
        res = sprintf(DRIVERNAME"(%s.%d,%d)",
         sid, protocol, state);
        break;
    }
    return res;
  }
}
