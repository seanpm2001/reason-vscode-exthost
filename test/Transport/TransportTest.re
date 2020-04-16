open TestFramework;
open Exthost;

module Header = Transport.Packet.Header;
module Packet = Transport.Packet;

let spawnNode = (~onExit, ~args) => {
  Luv.Process.spawn(
    ~on_exit=onExit,
    ~redirect=[
      Luv.Process.inherit_fd(
        ~fd=Luv.Process.stdin,
        ~from_parent_fd=Luv.Process.stdin,
        (),
      ),
      Luv.Process.inherit_fd(
        ~fd=Luv.Process.stdout,
        ~from_parent_fd=Luv.Process.stderr,
        (),
      ),
      Luv.Process.inherit_fd(
        ~fd=Luv.Process.stderr,
        ~from_parent_fd=Luv.Process.stderr,
        (),
      ),
    ],
    "node",
    ["node", ...args],
  )
  |> Result.get_ok;
};

module Waiter = {
  let wait = (~name="TODO", condition) => {
    let start = Unix.gettimeofday();
    let delta = () => Unix.gettimeofday() -. start;

    while (!condition() && delta() < 1.0) {
      let _: bool = Luv.Loop.run(~mode=`NOWAIT, ());
      Unix.sleepf(0.1);
    };

    if (!condition()) {
      failwith("Condition failed: " ++ name);
    };
  };

  let waitForCollection = (~name, item) => {
    let collected = ref(false);

    let checkCollected = () => {
      Gc.full_major();
      collected^ == true;
    };

    Gc.finalise_last(() => collected := true, item);
    wait(~name="Waiting for GC to collect: " ++ name, checkCollected);
  };
};

module Test = {
  type t = {
    exits: ref(bool),
    messages: ref(list(Transport.msg)),
    transport: Transport.t,
  };

  let uniqueId = ref(0);

  let start = scriptPath => {
    let namedPipe =
      NamedPipe.create("transport-test-" ++ (uniqueId^ |> string_of_int))
      |> NamedPipe.toString;
    incr(uniqueId);
    let messages = ref([]);
    let dispatch = msg => messages := [msg, ...messages^];

    let exits = ref(false);
    let onExit = (proc, ~exit_status, ~term_signal) => exits := true;
    let _: Luv.Process.t = spawnNode(~onExit, ~args=[scriptPath, namedPipe]);
    let transport = Transport.start(~namedPipe, ~dispatch) |> Result.get_ok;

    {exits, messages, transport};
  };

  let waitForMessagef = (~name, f, {messages, _} as context) => {
    Waiter.wait(() => messages^ |> List.exists(f));
    context;
  };

  let waitForMessage = (~name, msg, context) => {
    waitForMessagef(~name, m => m == msg, context);
  };

  let closeTransport = ({transport, _} as context) => {
    Transport.close(transport);
    context;
  };

  let send = (packet, {transport, _} as context) => {
    Transport.send(packet, transport);
    context;
  };

  let waitForExit = ({exits, _} as context) => {
    Waiter.wait(() => exits^);
    context;
  };
};

describe("Transport", ({describe, _}) => {
  describe("process sanity checks", ({test, _}) => {
    test("start node", ({expect}) => {
      let exits = ref(false);
      let onExit = (proc, ~exit_status, ~term_signal) => exits := true;
      let _ = spawnNode(~onExit, ~args=["--version"]);

      Waiter.wait(() => exits^ == true);
    });

    test("node process GC", ({expect}) => {
      let exits = ref(false);
      let onExit = (proc, ~exit_status, ~term_signal) => exits := true;
      let _proc: Luv.Process.t = spawnNode(~onExit, ~args=["--version"]);
      Waiter.wait(() => exits^ == true);
      // TODO:
      // waitForCollection(~name="proc", proc);
      //Gc.finalise_last(() => collected := true, proc);
      //wait(checkCollected);
    });
  });

  describe("server", ({test, _}) => {
    test("disconnect from server--side", ({expect}) => {
      let {transport, _}: Test.t =
        Test.start("node/client.js")
        |> Test.waitForMessage(~name="connect", Transport.Connected)
        |> Test.closeTransport
        |> Test.waitForExit;

      Waiter.waitForCollection(~name="transport", transport);
    });
    test("disconnect from client-side", ({expect}) => {
      let {transport, _}: Test.t =
        Test.start("node/immediate-disconnect-client.js")
        |> Test.waitForMessage(~name="connect", Transport.Connected)
        |> Test.waitForExit
        |> Test.waitForMessage(~name="disconnect", Transport.Disconnected);

      Waiter.waitForCollection(~name="transport", transport);
    });
    test("echo", ({expect}) => {
      let bytes = "Hello, world!" |> Bytes.of_string;
      let packet =
        Transport.Packet.create(~bytes, ~packetType=Regular, ~id=0);

      let {transport, _}: Test.t =
        Test.start("node/echo-client.js")
        |> Test.waitForMessage(~name="connect", Transport.Connected)
        |> Test.send(packet)
        |> Test.waitForMessagef(
             ~name="echo reply",
             fun
             | Transport.Received(packet) =>
               packet.Packet.body |> Bytes.to_string == "Hello, world!"
             | _ => false,
           )
        |> Test.closeTransport
        |> Test.waitForExit;

      Waiter.waitForCollection(~name="transport", transport);
    });
  });
});