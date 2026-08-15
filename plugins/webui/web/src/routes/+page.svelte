<script lang="ts">
  import { Sverminal, SverminalWriter } from "sverminal";
  import type { SverminalConfiguration } from "sverminal";
  import { bu } from "$lib/bu";

  let ws = new WebSocket(`${bu("ws:")}ws`);
  let promptPrefix = $state("(not connected) >");
  let promptPrefixQ = $state<string[]>([]);
  let autoCompletes = $state<string[]>([]);
  let waitForResponse = $state(false);
  let waitForUpload = $state(false);

  const config: SverminalConfiguration = {
    promptSuffix: "",
    style: {
      prompt: ["text-emerald-400"],
      command: ["text-violet-400"],
      flags: ["text-slate-400"],
      info: ["text-cyan-400"],
      error: ["text-red-400"],
      warn: ["text-yellow-400"],
      text: ["text-slate-50"],
    },
    history: {
      enabled: true,
      method: "memory", //memory (default), sessionstorage (tbd), localstorage (tbd)
      limit: 10,
    },
    newlineBetweenCommands: false,
    quoteMultiWordAutoCompletes: true,
  };

  let writer = new SverminalWriter();

  ws.onopen = () => {
    promptPrefixQ.push("(no tool)");
    ws.send(JSON.stringify({ type: "getCmds" }));
    ws.send(JSON.stringify({ type: "getPrompt" }));
  };

  ws.onclose = () => {
    promptPrefixQ.push("(not connected)");
    writer.error("Disconnected from server");
  };

  ws.onerror = (ev) => {
    writer.error("WebSocket error");
    writer.error(ev.toString());
  };

  ws.onmessage = (event) => {
    let data = JSON.parse(event.data);
    if (data.type === "cmds") {
      autoCompletes.push(...Object.keys(data.global));
      autoCompletes.push(...Object.keys(data.current));
    }
    if (data.type === "response") {
      writer.info(data.message);
      waitForResponse = false;
    }
    if (data.type === "prompt") {
      promptPrefixQ.push(data.prompt);
      waitForResponse = false;
    }
    if (data.type === "upload") {
      if (data.status === "success") {
        writer.info(`File uploaded successfully: ${data.filename}`);
      } else {
        writer.error(`File upload failed: ${data.filename}`);
        writer.error(`Error: ${data.message}`);
      }
      waitForUpload = false;
    }
    if (data.type === "ac") {
      writer.info(`Current client connections: ${data.count}`);
      waitForResponse = false;
    }
  };

  async function processor(command: string): Promise<void> {
    let p = document.createElement("p");
    p.innerHTML = promptPrefix;
    if (command.startsWith(p.innerText)) {
      command = command.substring(p.innerText.length).trim();
    }
    let splits = command.split(" ");
    if (splits[0].startsWith("!")) {
      let lcmd = command.substring(1).split(" ");
      if (lcmd[0] === "clear") {
        writer.clear();
      } else if (lcmd[0] === "prompt") {
        writer.info(`Current prompt prefix: ${promptPrefix}`);
      } else if (lcmd[0] === "getac") {
        ws.send(JSON.stringify({ type: "getAC" }));
        waitForResponse = true;
        while (waitForResponse) {
          await new Promise((resolve) => setTimeout(resolve, 100));
        }
      } else if (lcmd[0] === "upload") {
        if (lcmd.length < 2) {
          writer.error("Usage: !upload <filename>");
          return;
        }
        let varname = lcmd[1];
        let fileInput = document.createElement("input");
        fileInput.type = "file";
        fileInput.onchange = () => {
          let file = fileInput.files?.[0];
          if (file) {
            let reader = new FileReader();
            reader.onload = () => {
              let content = btoa(reader.result as string);
              ws.send(JSON.stringify({ type: "upload", varname, content }));
              writer.info(`Uploaded file: ${varname}`);
            };
            reader.readAsText(file);
          } else {
            writer.error("No file selected");
          }
        };
        fileInput.click();
        waitForUpload = true;
        while (waitForUpload) {
          await new Promise((resolve) => setTimeout(resolve, 100));
        }
        writer.info(`File uploaded and bound to variable: ${varname}`);
      } else if (lcmd[0] === "help") {
        writer.info("Available commands:");
        writer.info("!clear - Clear the terminal");
        writer.info("!help - Show this help message");
        writer.info("!prompt - Show the current prompt prefix");
        writer.info(
          "!getac - Get the current client connections count form the server",
        );
        writer.info(
          "!upload <varname> - Upload a file to the server and bind it to a variable",
        );
      } else {
        writer.error(`Unknown local command: ${lcmd[0]}`);
      }
      return;
    }
    waitForResponse = true;
    ws.send(
      JSON.stringify({
        type: "input",
        input: command + "\n",
      }),
    );
    ws.send(
      JSON.stringify({
        type: "getPrompt",
      }),
    );
    while (waitForResponse) {
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    if (promptPrefixQ.length > 0) {
      promptPrefix = promptPrefixQ[promptPrefixQ.length - 1];
      console.log(`Prompt prefix updated to: ${promptPrefix}`);
      promptPrefixQ = [];
    }
    new Promise((resolve) => {
      setTimeout(resolve, 10);
    }).then(() => {
      let sverminal = document.getElementById("sverminal");
      if (sverminal) {
        sverminal.scrollTop = sverminal.scrollHeight;
      }
    });
  }
</script>

<div class="flex flex-col h-screen">
  <header class="p-4 text-center"></header>
  <div class="term flex-grow overflow-scroll h-max" id="sverminal">
    <Sverminal {processor} {writer} {config} {promptPrefix} {autoCompletes} />
  </div>
  <footer>
    <div class="container mx-auto text-center py-4">
      <p class="text-slate-400">
        Powered by <a
          href="https://sverminal.io"
          class="text-cyan-400"
          target="_blank"
          rel="noopener noreferrer">Sverminal</a
        >
      </p>
    </div>
  </footer>
</div>
<!-- Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:  -->
