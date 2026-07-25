import { API_HOST } from "$app/env/public";

export function bu(protocol: string = "http:") {
  let host = API_HOST;
  if (document.location.protocol === "https:") {
    if (protocol === "http:") {
      protocol = "https:";
    } else if (protocol === "ws:") {
      protocol = "wss:";
    }
  }
  if (host === "") {
    host = `${protocol}//${document.location.host}/`;
  } else {
    host = `${protocol}//${host}/`;
  }
  console.log(host);
  return host;
}
