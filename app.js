const $ = (id) => document.getElementById(id);
const messages = $("messages"), input = $("messageInput"), form = $("chatForm");
const welcome = $("welcome"), drawer = $("fileDrawer"), scrim = $("scrim");
function addMessage(text, role) {
  welcome.hidden = true;
  const row = document.createElement("div"); row.className = `message ${role}`;
  const avatar = document.createElement("div"); avatar.className = "avatar"; avatar.textContent = role === "user" ? "K" : "M";
  const bubble = document.createElement("div"); bubble.className = "bubble"; bubble.textContent = text;
  row.append(avatar, bubble); messages.append(row);
  $("chatForm").scrollIntoView({block:"end",behavior:"smooth"});
}
async function api(path, data) {
  const opts = data ? {method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(data)} : {};
  const res = await fetch(path,opts);
  if(!res.ok) throw new Error("Server merespons HTTP " + res.status);
  return res.json();
}
async function sendMessage(text) {
  text = text.trim(); if(!text) return;
  addMessage(text,"user"); input.value=""; input.style.height="auto"; $("sendBtn").disabled=true;
  try { const result = await api("/api/chat",{message:text}); addMessage(result.reply || "Tidak ada balasan.","assistant"); }
  catch(e) { addMessage("Tidak dapat terhubung ke server C++. Pastikan run.py masih berjalan. ("+e.message+")","assistant"); }
  finally { $("sendBtn").disabled=false; input.focus(); }
}
form.addEventListener("submit",e=>{e.preventDefault();sendMessage(input.value);});
input.addEventListener("input",()=>{input.style.height="auto";input.style.height=Math.min(input.scrollHeight,150)+"px";});
input.addEventListener("keydown",e=>{if(e.key==="Enter"&&!e.shiftKey){e.preventDefault();form.requestSubmit();}});
document.querySelectorAll("[data-prompt]").forEach(b=>b.addEventListener("click",()=>sendMessage(b.dataset.prompt)));
function openDrawer(){drawer.classList.add("open");scrim.classList.add("show");loadFiles();}
function closeDrawer(){drawer.classList.remove("open");scrim.classList.remove("show");}
$("openFiles").addEventListener("click",openDrawer); $("quickFiles").addEventListener("click",openDrawer);
$("closeFiles").addEventListener("click",closeDrawer); scrim.addEventListener("click",closeDrawer);
$("refreshFiles").addEventListener("click",loadFiles);
async function loadFiles(){
  const list=$("fileList"); list.replaceChildren();
  try {
    const data=await api("/api/files");
    if(!data.files || !data.files.length){const p=document.createElement("p");p.className="muted";p.textContent="Belum ada executable. Tambahkan program executable ke folder Maghribi/.";list.append(p);return;}
    data.files.forEach(name=>{
      const card=document.createElement("div");card.className="file-card";
      const icon=document.createElement("div");icon.className="file-icon";icon.textContent="▣";
      const info=document.createElement("div");info.className="file-info";
      const title=document.createElement("strong");title.textContent=name;
      const sub=document.createElement("small");sub.textContent="Executable lokal";
      const btn=document.createElement("button");btn.className="run-btn";btn.textContent="Jalankan";
      btn.addEventListener("click",async()=>{btn.disabled=true;try{const r=await api("/api/run",{file:name});$("runMessage").textContent=r.message||"Selesai.";if(!r.ok)$("runMessage").textContent=r.message||"Gagal menjalankan."; }catch(e){$("runMessage").textContent="Gagal: "+e.message;}finally{btn.disabled=false;}});
      info.append(title,sub);card.append(icon,info,btn);list.append(card);
    });
  } catch(e){const p=document.createElement("p");p.className="muted";p.textContent="Tidak dapat memuat file. Pastikan server aktif.";list.append(p);}
}
api("/api/status").then(()=> $("statusDot").classList.add("online")).catch(()=>{});
