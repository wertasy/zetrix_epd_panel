/**
 * @file ap_transfer_server.c
 * @brief WiFi AP + HTTP server implementation for image transfer — C port of
 *        C++ rawdraw::ApTransferServer.
 *
 * Serves a self-contained upload page over esp_http_server while the device
 * is in AP mode (192.168.4.1), receives raw 1bpp / BWRY-2bpp payloads and
 * saves them via photo_storage.
 *
 * Note on station coordination: the C++ original suspends the WiFi station
 * event handlers while the AP runs. The C port keeps this file self-contained
 * (main/wifi_manager.c is untouched); a transient station reconnect attempt
 * during AP mode fails with a tolerated error (same tolerant style as the
 * original C++), and the station reconnects normally after Stop().
 */
#include "ap_transfer_server.h"

#include "photo_storage.h"
#include "style.h"
#include "cJSON.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <nvs_flash.h>
#include <lwip/ip_addr.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TAG "ApTransferServer"

/* AP configuration */
#define AP_SSID "InkScreen-AP"
#define AP_PASSWORD "12345678"
#define AP_IP "192.168.4.1"
#define GALLERY_NAMESPACE "gallery"
#define SLIDESHOW_INTERVAL_KEY "slide_min"

/* Screen dimensions */
#define AP_SCREEN_WIDTH STYLE_SCREEN_WIDTH
#define AP_SCREEN_HEIGHT STYLE_SCREEN_HEIGHT
#define AP_IMAGE_1BPP_SIZE STYLE_SCREEN_1BPP_BYTES /* 15000 */
#define AP_IMAGE_2BPP_SIZE STYLE_SCREEN_2BPP_BYTES /* 30000 */

/* BOOT button on the ZecTrix-S3 board (see main/config.h BOOT_BUTTON_GPIO). */
#define AP_BOOT_BUTTON_GPIO GPIO_NUM_0

/* Embedded HTML. Kept self-contained because the ESP-IDF HTTP server serves
 * this page from flash while the device is in AP mode. */
static const char kUploadHtml[] =
    "\n"
    "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>墨水屏传图</title>\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1\">\n"
    "<style>\n"
    "*{box-sizing:border-box}body{margin:0;background:#ece8dc;color:#171717;font-family:-apple-system,"
    "BlinkMacSystemFont,\"Segoe UI\",sans-serif;font-size:12px}.app{max-width:520px;margin:0 "
    "auto;padding:10px}.top{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}.brand{"
    "font-weight:800;font-size:16px}.pill{border:1px solid #111;background:#ffd900;border-radius:3px;padding:3px "
    "6px;font-size:11px}.panel{background:#fff;border:2px solid #111;border-radius:6px;box-shadow:3px 3px 0 "
    "#111;margin-bottom:10px;padding:9px}.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.muted{color:#555}"
    ".btn{border:2px solid #111;background:#ff3b30;color:#fff;border-radius:5px;padding:8px "
    "10px;font-weight:800;font-size:12px;box-shadow:2px 2px 0 "
    "#111}.btn.secondary{background:#fff;color:#111}.btn.yellow{background:#ffd900;color:#111}.btn.danger{background:#"
    "111;color:#fff}.btn.icon{width:32px;height:32px;border-radius:50%;padding:0;font-size:18px;line-height:1}.btn:"
    "disabled{opacity:.45}.file{position:absolute;left:-9999px}.radio{display:inline-flex;gap:5px;align-items:center;"
    "border:1px solid #111;border-radius:4px;padding:5px 7px;background:#fafafa}.radio "
    "input{margin:0}.preview{width:100%;aspect-ratio:4/3;border:2px solid "
    "#111;background:#fff;image-rendering:pixelated;margin-top:8px}.grid{display:grid;grid-template-columns:repeat(2,"
    "minmax(0,1fr));gap:8px}.card{border:2px solid "
    "#111;border-radius:5px;background:#fff;overflow:hidden;position:relative}.thumb{width:100%;aspect-ratio:4/"
    "3;background:#f8f8f8;display:block;image-rendering:pixelated}.meta{padding:6px}.title{font-weight:800;white-space:"
    "nowrap;overflow:hidden;text-overflow:ellipsis}.body{font-size:11px;color:#444;line-height:1.35;height:30px;"
    "overflow:hidden}.check{position:absolute;top:5px;left:5px;width:20px;height:20px}.tag{position:absolute;top:5px;"
    "right:5px;background:#ffd900;border:1px solid #111;border-radius:3px;padding:2px "
    "4px;font-size:10px}.bar{display:flex;align-items:center;justify-content:space-between;gap:6px;margin:8px "
    "0}.status{min-height:18px;color:#333}.note{border:2px solid "
    "#111;background:#fffbe6;border-radius:6px;padding:9px;margin-bottom:10px;box-shadow:3px 3px 0 #111}.note "
    "ul{margin:6px 0 0 "
    "18px;padding:0;line-height:1.55}.modal{position:fixed;inset:0;background:rgba(0,0,0,.45);display:none;align-items:"
    "center;justify-content:center;padding:12px}.modal.open{display:flex}.dialog{max-width:520px;width:100%;background:"
    "#fff;border:2px solid #111;border-radius:7px;box-shadow:4px 4px 0 "
    "#111;position:relative;padding:10px}.close{position:absolute;right:8px;top:8px;border:2px solid "
    "#111;background:#fff;border-radius:50%;width:28px;height:28px;font-weight:900}.big{width:100%;aspect-ratio:4/"
    "3;border:2px solid #111;image-rendering:pixelated}.empty{padding:18px;text-align:center;border:1px dashed "
    "#999;background:#fafafa}.split{display:grid;grid-template-columns:1fr;gap:8px}@media(min-width:460px){.split{grid-"
    "template-columns:190px 1fr}.grid{grid-template-columns:repeat(3,minmax(0,1fr))}}\n"
    "</style></head><body><main class=\"app\">\n"
    "<div class=\"top\"><div><div class=\"brand\">墨水屏传图</div><div class=\"muted\" "
    "id=\"endpoint\">读取服务地址...</div></div><div class=\"row\"><button class=\"btn secondary\" "
    "id=\"settingsBtn\">设置</button><button class=\"btn secondary icon\" id=\"helpBtn\">!</button><div "
    "class=\"pill\">400x300</div></div></div>\n"
    "<section class=\"note\" id=\"helpPanel\" style=\"display:none\"><b>功能说明</b><ul><li>支持 1 BP 黑白和 2 BP "
    "四色图片上传，保存后可在设备相册查看。</"
    "li><li>轮播关闭后，大图会固定停在当前图片；开启后，设备在相册大图模式按周期自动切换。</"
    "li><li>局域网服务开启时，页面顶部会显示设备本地 IP，可用手机或 NAS/本地 server "
    "管理图片。</li><li>关闭服务只停止本地传图网页；关闭并省电会停止服务和 WiFi，进入 deep sleep，按 BOOT "
    "唤醒。</li><li>极致省电建议：选好大图，关闭轮播，再执行关闭并省电，墨水屏会保留最后画面。</li></ul></section>\n"
    "<section class=\"panel split\"><div><div class=\"title\">发送图片</div><p "
    "class=\"muted\">先选择图片，预览转换效果，再发送到设备。</p><div class=\"row\"><label class=\"radio\"><input "
    "name=\"fmt\" type=\"radio\" value=\"1bpp\" checked>1 BP 黑白</label><label class=\"radio\"><input name=\"fmt\" "
    "type=\"radio\" value=\"bwry2bpp\">2 BP 四色</label></div><div class=\"row\" style=\"margin-top:8px\"><button "
    "class=\"btn yellow\" id=\"pick\">选择图片</button><button class=\"btn\" id=\"send\" "
    "disabled>发送</button></div><input class=\"file\" id=\"file\" type=\"file\" accept=\"image/*\"><div "
    "class=\"status\" id=\"status\">等待选择</div></div><div><canvas class=\"preview\" id=\"preview\" width=\"400\" "
    "height=\"300\"></canvas></div></section>\n"
    "<section class=\"panel\" id=\"settingsPanel\" style=\"display:none\"><div class=\"bar\"><b>相册轮播周期</b><span "
    "class=\"muted\" id=\"settingsState\"></span></div><div class=\"row\"><label class=\"radio\"><input name=\"slide\" "
    "type=\"radio\" value=\"0\">关闭</label><label class=\"radio\"><input name=\"slide\" type=\"radio\" "
    "value=\"5\">5min</label><label class=\"radio\"><input name=\"slide\" type=\"radio\" "
    "value=\"10\">10min</label><label class=\"radio\"><input name=\"slide\" type=\"radio\" "
    "value=\"30\">30min</label><button class=\"btn yellow\" id=\"saveSettings\">保存设置</button><button class=\"btn "
    "secondary\" id=\"stopService\">关闭服务</button><button class=\"btn danger\" "
    "id=\"sleepNow\">关闭并省电</button></div></section>\n"
    "<section class=\"panel\"><div class=\"bar\"><div><b>设备图片</b> <span class=\"muted\" "
    "id=\"count\"></span></div><div class=\"row\"><button class=\"btn secondary\" id=\"reload\">刷新</button><button "
    "class=\"btn danger\" id=\"batch\" disabled>删除选中</button></div></div><div id=\"photos\" class=\"grid\"><div "
    "class=\"empty\">读取中...</div></div></section>\n"
    "</main>\n"
    "<div class=\"modal\" id=\"modal\"><div class=\"dialog\"><button class=\"close\" id=\"close\">×</button><canvas "
    "class=\"big\" id=\"big\" width=\"400\" height=\"300\"></canvas><div class=\"meta\"><input id=\"mTitle\" "
    "style=\"width:100%;padding:7px;border:1px solid #111;font-weight:800\"><div class=\"row\" "
    "style=\"margin-top:6px\"><input id=\"mDate\" placeholder=\"日期\" style=\"flex:1;padding:7px;border:1px solid "
    "#111\"><input id=\"mLocation\" placeholder=\"地点\" style=\"flex:1;padding:7px;border:1px solid "
    "#111\"></div><textarea id=\"mBody\" rows=\"3\" style=\"width:100%;margin-top:6px;padding:7px;border:1px solid "
    "#111\"></textarea><div class=\"muted\" id=\"mMeta\" style=\"margin-top:5px\"></div><div class=\"row\" "
    "style=\"margin-top:8px\"><button class=\"btn yellow\" id=\"mSave\">保存信息</button><button class=\"btn "
    "secondary\" id=\"mUp\">上移</button><button class=\"btn secondary\" id=\"mDown\">下移</button><button class=\"btn "
    "danger\" id=\"mDelete\">删除这张</button></div></div></div></div>\n"
    "<script>\n"
    "const "
    "W=400,H=300,photosEl=document.getElementById('photos'),statusEl=document.getElementById('status'),pv=document."
    "getElementById('preview'),fileEl=document.getElementById('file'),sendBtn=document.getElementById('send'),batchBtn="
    "document.getElementById('batch'),countEl=document.getElementById('count'),settingsPanel=document.getElementById('"
    "settingsPanel'),settingsState=document.getElementById('settingsState'),endpointEl=document.getElementById('"
    "endpoint');let pending=null,pendingFmt='1bpp',items=[],selected=new Set(),active=null;\n"
    "async function loadStatus(){try{const j=await (await "
    "fetch('/status')).json();endpointEl.textContent=`${j.mode==='lan'?'LAN':'InkScreen-AP'} / "
    "${j.ip}`;document.title=`墨水屏传图 ${j.ip}`}catch(e){endpointEl.textContent='服务地址读取失败'}}\n"
    "function fmt(){return document.querySelector('input[name=fmt]:checked').value}\n"
    "function rgba(c){return c===0?[0,0,0]:c===1?[255,255,255]:c===2?[255,217,0]:[220,0,0]}\n"
    "function draw1(buf,canvas){const ctx=canvas.getContext('2d'),img=ctx.createImageData(W,H),d=img.data;for(let "
    "p=0,i=0;p<W*H;p++,i+=4){const "
    "v=(buf[p>>3]&(1<<(7-(p&7))))?255:0;d[i]=d[i+1]=d[i+2]=v;d[i+3]=255}ctx.putImageData(img,0,0)}\n"
    "function draw2(buf,canvas){const ctx=canvas.getContext('2d'),img=ctx.createImageData(W,H),d=img.data;for(let "
    "p=0,i=0;p<W*H;p++,i+=4){const "
    "b=buf[p>>2],c=(b>>(6-((p&3)*2)))&3,r=rgba(c);d[i]=r[0];d[i+1]=r[1];d[i+2]=r[2];d[i+3]=255}ctx.putImageData(img,0,"
    "0)}\n"
    "function fitImage(file){return new Promise((res,rej)=>{const img=new Image();img.onload=()=>{const "
    "c=document.createElement('canvas');c.width=W;c.height=H;const "
    "x=c.getContext('2d',{willReadFrequently:true});x.fillStyle='#fff';x.fillRect(0,0,W,H);const "
    "s=Math.min(W/img.width,H/img.height),w=Math.round(img.width*s),h=Math.round(img.height*s);x.drawImage(img,(W-w)/"
    "2,(H-h)/"
    "2,w,h);URL.revokeObjectURL(img.src);res(x.getImageData(0,0,W,H).data)};img.onerror=rej;img.src=URL."
    "createObjectURL(file)})}\n"
    "async function convert(file){const data=await "
    "fitImage(file),mode=fmt();pendingFmt=mode;if(mode==='bwry2bpp'){const work=new Array(H);for(let "
    "y=0;y<H;y++){work[y]=new Array(W);for(let x=0;x<W;x++){const "
    "i=(y*W+x)*4;work[y][x]={r:data[i],g:data[i+1],b:data[i+2]}}}const "
    "pal=[[0,0,0],[255,255,255],[255,0,0],[255,255,0]];const out=new Uint8Array(30000);for(let y=0;y<H;y++)for(let "
    "x=0;x<W;x++){const old=work[y][x];let minD=1e9,cIdx=0,cRgb=pal[0];for(let k=0;k<4;k++){const "
    "d=0.299*(old.r-pal[k][0])**2+0.587*(old.g-pal[k][1])**2+0.114*(old.b-pal[k][2])**2;if(d<minD){minD=d;cIdx=k;cRgb="
    "pal[k]}}const bwry=cIdx===0?0:cIdx===1?1:cIdx===2?3:2;out[(y*W+x)>>2]|=bwry<<(6-((y*W+x)&3)*2);const "
    "eR=old.r-cRgb[0],eG=old.g-cRgb[1],eB=old.b-cRgb[2];const dist=(dy,dx,f)=>{const "
    "ny=y+dy,nx=x+dx;if(ny>=0&&ny<H&&nx>=0&&nx<W){work[ny][nx].r+=eR*f;work[ny][nx].g+=eG*f;work[ny][nx].b+=eB*f}};"
    "dist(0,1,7/16);dist(1,-1,3/16);dist(1,0,5/16);dist(1,1,1/16)}draw2(out,pv);return out}const gray=new "
    "Int16Array(W*H);for(let p=0,i=0;p<gray.length;p++,i+=4)gray[p]=(data[i]*30+data[i+1]*59+data[i+2]*11)/100|0;const "
    "out=new Uint8Array(15000);for(let y=0;y<H;y++)for(let x=0;x<W;x++){const "
    "p=y*W+x,old=Math.max(0,Math.min(255,gray[p])),nw=old>128?255:0,err=old-nw;if(nw>128)out[p>>3]|=1<<(7-(p&7));if(x+"
    "1<W)gray[p+1]+=err*7/16;if(y+1<H){if(x>0)gray[p+W-1]+=err*3/16;gray[p+W]+=err*5/16;if(x+1<W)gray[p+W+1]+=err/"
    "16}}draw1(out,pv);return out}\n"
    "document.getElementById('pick').onclick=()=>fileEl.click();fileEl.onchange=async()=>{const "
    "f=fileEl.files[0];if(!f)return;statusEl.textContent='转换中...';try{pending=await "
    "convert(f);sendBtn.disabled=false;statusEl.textContent=`已预览 ${pendingFmt==='bwry2bpp'?'2 BP 四色':'1 BP "
    "黑白'}，确认后点击发送`}catch(e){statusEl.textContent='图片处理失败';sendBtn.disabled=true}};document."
    "querySelectorAll('input[name=fmt]').forEach(r=>r.onchange=()=>{if(fileEl.files[0])fileEl.onchange()});\n"
    "sendBtn.onclick=async()=>{if(!pending)return;sendBtn.disabled=true;statusEl.textContent='上传中...';try{const "
    "r=await "
    "fetch('/upload?format='+encodeURIComponent(pendingFmt),{method:'POST',headers:{'Content-Type':'application/"
    "octet-stream'},body:pending});const j=await r.json();statusEl.textContent=j.success?'已发送并保存':'失败: "
    "'+(j.error||'unknown');await loadPhotos()}catch(e){statusEl.textContent='网络错误'}sendBtn.disabled=false};\n"
    "async function loadBin(p,c){const b=new Uint8Array(await (await "
    "fetch('/"
    "photo?id='+encodeURIComponent(p.id),{cache:'no-store'})).arrayBuffer());(p.format==='bwry2bpp'||p.size>15000?"
    "draw2:draw1)(b,c)}\n"
    "function updateBatch(){batchBtn.disabled=selected.size===0}\n"
    "async function loadPhotos(){selected.clear();updateBatch();try{const j=await (await "
    "fetch('/photos',{cache:'no-store'})).json();items=j.photos||[];countEl.textContent=`${items.length} "
    "张`;photosEl.innerHTML=items.length?'':'<div class=\"empty\">暂无图片</div>';const thumbs=[];for(const p of "
    "items){const card=document.createElement('div');card.className='card';card.innerHTML=`<input class=\"check\" "
    "type=\"checkbox\"><span class=\"tag\">${p.format==='bwry2bpp'?'2BP':'1BP'}</span><canvas class=\"thumb\" "
    "width=\"400\" height=\"300\"></canvas><div class=\"meta\"><div class=\"title\">${p.title||p.id}</div><div "
    "class=\"body\">${p.body||''}</div><div class=\"muted\">${p.date||''} ${p.location||''}</div><div class=\"row\" "
    "style=\"margin-top:5px\"><button class=\"btn yellow show\">展示</button><button class=\"btn secondary "
    "up\">上移</button><button class=\"btn secondary down\">下移</button></div></div>`;const "
    "c=card.querySelector('canvas'),ck=card.querySelector('input');ck.onclick=e=>{e.stopPropagation();ck.checked?"
    "selected.add(p.id):selected.delete(p.id);updateBatch()};card.querySelector('.show').onclick=e=>{e.stopPropagation("
    ");showPhoto(p.id)};card.querySelector('.up').onclick=e=>{e.stopPropagation();movePhoto(p.id,-1)};card."
    "querySelector('.down').onclick=e=>{e.stopPropagation();movePhoto(p.id,1)};card.onclick=()=>openModal(p);photosEl."
    "appendChild(card);thumbs.push([p,c])}for(const [p,c] of thumbs){await "
    "loadBin(p,c).catch(()=>{})}}catch(e){photosEl.innerHTML='<div class=\"empty\">读取失败</div>'}}\n"
    "function "
    "openModal(p){active=p;document.getElementById('modal').classList.add('open');document.getElementById('mTitle')."
    "value=p.title||'';document.getElementById('mDate').value=p.date||'';document.getElementById('mLocation').value=p."
    "location||'';document.getElementById('mMeta').textContent=`${p.format==='bwry2bpp'?'2 BP 四色':'1 BP 黑白'} · "
    "${p.width}x${p.height}`;document.getElementById('mBody').value=p.body||'';loadBin(p,document.getElementById('big')"
    ").catch(()=>{})}\n"
    "document.getElementById('close').onclick=()=>document.getElementById('modal').classList.remove('open');document."
    "getElementById('modal').onclick=e=>{if(e.target.id==='modal')document.getElementById('modal').classList.remove('"
    "open')};\n"
    "async function delOne(id){return fetch('/photo?id='+encodeURIComponent(id),{method:'DELETE'}).then(r=>r.json())}\n"
    "document.getElementById('mDelete').onclick=async()=>{if(!active)return;if(!confirm('确认删除这张图片？'))return;"
    "await delOne(active.id);document.getElementById('modal').classList.remove('open');loadPhotos()};\n"
    "async function movePhoto(id,delta){await "
    "fetch('/photos/move',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({id,delta})});await loadPhotos()}\n"
    "async function showPhoto(id){const j=await (await "
    "fetch('/photo/show',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({id})})).json();statusEl.textContent=j.success?'已切到设备大图':'展示失败'}\n"
    "document.getElementById('mUp').onclick=()=>active&&movePhoto(active.id,-1);\n"
    "document.getElementById('mDown').onclick=()=>active&&movePhoto(active.id,1);\n"
    "document.getElementById('mSave').onclick=async()=>{if(!active)return;const "
    "body={id:active.id,title:document.getElementById('mTitle').value,date:document.getElementById('mDate').value,"
    "location:document.getElementById('mLocation').value,body:document.getElementById('mBody').value};const r=await "
    "fetch('/photo/meta',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify(body)});if((await "
    "r.json()).success){document.getElementById('modal').classList.remove('open');await loadPhotos()}};\n"
    "document.getElementById('settingsBtn').onclick=()=>{settingsPanel.style.display=settingsPanel.style.display==='"
    "none'?'block':'none';loadSettings()};\n"
    "document.getElementById('helpBtn').onclick=()=>{const "
    "p=document.getElementById('helpPanel');p.style.display=p.style.display==='none'?'block':'none'};\n"
    "async function loadSettings(){try{const j=await (await "
    "fetch('/"
    "settings')).json();document.querySelectorAll('input[name=slide]').forEach(r=>r.checked=Number(r.value)===j."
    "slideshow_interval);const slide=j.slideshow_interval?`轮播 ${j.slideshow_interval}min`:'轮播关闭';const "
    "svc=j.service_running?`服务开启 ${j.url||''}`:'服务将关闭';settingsState.textContent=`${slide} · "
    "${svc}`}catch(e){settingsState.textContent='读取失败'}}\n"
    "document.getElementById('saveSettings').onclick=async()=>{const "
    "v=Number(document.querySelector('input[name=slide]:checked')?.value||0);const j=await (await "
    "fetch('/settings',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({slideshow_interval:v})})).json();settingsState.textContent=j.success?(v?`已保存 "
    "${v}min`:'已关闭'):'保存失败'};\n"
    "document.getElementById('stopService').onclick=async()=>{if(!confirm('关闭本地传图服务？'))return;await "
    "fetch('/settings',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({service_enabled:false})});settingsState.textContent='服务正在关闭'};\n"
    "document.getElementById('sleepNow').onclick=async()=>{if(!confirm('关闭服务、WiFi 并进入省电模式？'))return;await "
    "fetch('/settings',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({service_enabled:false,wifi_enabled:false,sleep:true})});settingsState.textContent='"
    "设备正在进入省电模式'};\n"
    "batchBtn.onclick=async()=>{const ids=[...selected];if(!ids.length)return;if(!confirm(`确认删除 ${ids.length} "
    "张图片？`))return;for(const id of ids)await "
    "delOne(id);loadPhotos()};document.getElementById('reload').onclick=loadPhotos;loadStatus();loadSettings();"
    "loadPhotos();\n"
    "</script></body></html>\n";

/* Forward declaration (defined in the state-notification section below). */
static void notify_state(ap_transfer_server_t *server, ap_server_state_t state, const char *message);

/* ------------------------------------------------------------------ */
/* JSON / session helpers                                              */
/* ------------------------------------------------------------------ */

static cJSON *read_json_body(httpd_req_t *req)
{
    if (!req || req->content_len == 0 || req->content_len > 2048)
        return NULL;
    char *buf = (char *)calloc(1, (size_t)req->content_len + 1);
    if (!buf)
        return NULL;
    size_t received = 0;
    while (received < (size_t)req->content_len) {
        int ret = httpd_req_recv(req, buf + received, (size_t)req->content_len - received);
        if (ret <= 0) {
            free(buf);
            return NULL;
        }
        received += (size_t)ret;
    }
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    return root;
}

static void close_current_session(httpd_req_t *req)
{
    if (!req || !req->handle)
        return;
    const int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0)
        return;
    esp_err_t err = httpd_sess_trigger_close(req->handle, sockfd);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "httpd_sess_trigger_close(%d) failed: %s", sockfd, esp_err_to_name(err));
    }
}

static void send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    close_current_session(req);
}

static void copy_json_string(cJSON *root, const char *key, char *out, size_t out_size)
{
    if (!root || !key || !out || out_size == 0)
        return;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring) {
        strlcpy(out, item->valuestring, out_size);
    }
}

/* ------------------------------------------------------------------ */
/* Deferred web-control (stop service / stop wifi / deep sleep)        */
/* ------------------------------------------------------------------ */

typedef struct {
    ap_transfer_server_t *server;
    bool                  stop_wifi;
    bool                  enter_sleep;
} deferred_control_request_t;

static void deferred_control_task(void *arg)
{
    deferred_control_request_t *request = (deferred_control_request_t *)arg;
    if (!request) {
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(300));
    if (request->server) {
        ap_transfer_server_stop(request->server);
    }
    if (request->stop_wifi || request->enter_sleep) {
        ESP_LOGI(TAG, "Stopping WiFi after web control request");
        esp_wifi_disconnect();
        esp_wifi_stop();
    }
    if (request->enter_sleep) {
        ESP_LOGI(TAG, "Entering deep sleep after web control request");
        esp_sleep_enable_ext0_wakeup(AP_BOOT_BUTTON_GPIO, 0);
        esp_deep_sleep_start();
    }
    free(request);
    vTaskDelete(NULL);
}

static void schedule_deferred_control(ap_transfer_server_t *server, bool stop_wifi, bool enter_sleep)
{
    deferred_control_request_t *request = (deferred_control_request_t *)calloc(1, sizeof(*request));
    if (!request) {
        ESP_LOGE(TAG, "Failed to allocate deferred control request");
        return;
    }
    request->server      = server;
    request->stop_wifi   = stop_wifi;
    request->enter_sleep = enter_sleep;
    BaseType_t ok        = xTaskCreate(&deferred_control_task, "ap_web_control", 4096, request, 4, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create deferred control task");
        free(request);
    }
}

/* ------------------------------------------------------------------ */
/* HTTP handlers                                                       */
/* ------------------------------------------------------------------ */

static bool is_authorized(httpd_req_t *req)
{
    ap_transfer_server_t *self = (ap_transfer_server_t *)req->user_ctx;
    if (!self) {
        return false;
    }
    if (self->mode == AP_SERVER_MODE_AP) {
        return true;
    }
    char auth_hdr[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, sizeof(auth_hdr)) == ESP_OK) {
        if (strcmp(auth_hdr, "Bearer 12345678") == 0 || strcmp(auth_hdr, "12345678") == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t ret = httpd_resp_send(req, kUploadHtml, strlen(kUploadHtml));
    close_current_session(req);
    return ret;
}

static esp_err_t upload_handler(httpd_req_t *req)
{
    ap_transfer_server_t *self = (ap_transfer_server_t *)req->user_ctx;

    if (self) {
        notify_state(self, AP_SERVER_STATE_K_RECEIVING_IMAGE, "Receiving image...");
    }

    char query[96]  = {0};
    char format[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "format", format, sizeof(format));
    }
    const bool   is_2bpp       = strcmp(format, "bwry2bpp") == 0 || strcmp(format, "2bpp") == 0;
    const size_t expected_size = is_2bpp ? AP_IMAGE_2BPP_SIZE : AP_IMAGE_1BPP_SIZE;

    if ((size_t)req->content_len != expected_size) {
        ESP_LOGW(TAG, "Invalid upload size: %u", (unsigned)req->content_len);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Connection", "close");
        esp_err_t send_ret = httpd_resp_send(req,
                                             is_2bpp ? "{\"success\":false,\"error\":\"需要400x300 2bpp四色数据\"}"
                                                     : "{\"success\":false,\"error\":\"需要400x300 1bpp数据\"}",
                                             HTTPD_RESP_USE_STRLEN);
        close_current_session(req);
        return send_ret == ESP_OK ? ESP_OK : send_ret;
    }

    uint8_t *buf = (uint8_t *)malloc(expected_size);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < expected_size) {
        int ret = httpd_req_recv(req, (char *)(buf + received), expected_size - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Receive failed");
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }

    ESP_LOGI(TAG, "Received %u bytes", (unsigned)received);
    if (self) {
        notify_state(self, AP_SERVER_STATE_K_PROCESSING_IMAGE, "Processing...");
    }

    photo_info_t info;
    memset(&info, 0, sizeof(info));
    const uint32_t now = (uint32_t)time(NULL);
    const uint64_t ms  = (uint64_t)(esp_timer_get_time() / 1000);
    snprintf(info.id, sizeof(info.id), "ap%011llu", (unsigned long long)(ms % 100000000000ULL));
    snprintf(info.title, sizeof(info.title), is_2bpp ? "WiFi四色图片" : "WiFi黑白图片");
    snprintf(info.location, sizeof(info.location), "WiFi AP");
    snprintf(info.body, sizeof(info.body), is_2bpp ? "手机 WiFi 传图 · 2 BP 四色" : "手机 WiFi 传图 · 1 BP 黑白");
    info.width     = AP_SCREEN_WIDTH;
    info.height    = AP_SCREEN_HEIGHT;
    info.file_size = (uint32_t)expected_size;
    info.timestamp = now > 0 ? now : (uint32_t)(ms / 1000);

    if (now > 0) {
        time_t    t = now;
        struct tm tm_info;
        localtime_r(&t, &tm_info);
        strftime(info.date, sizeof(info.date), "%Y-%m-%d", &tm_info);
    }

    const bool saved = photo_save(&info, buf) == 0;
    free(buf);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    if (saved) {
        if (self) {
            notify_state(self, AP_SERVER_STATE_K_IMAGE_SAVED, info.id);
            if (self->image_received_cb) {
                self->image_received_cb(info.id, self->image_received_cb_ctx);
            }
            if (self->photos_changed_cb) {
                self->photos_changed_cb(self->photos_changed_cb_ctx);
            }
        }
        char response[96];
        snprintf(response, sizeof(response), "{\"success\":true,\"id\":\"%s\"}", info.id);
        esp_err_t send_ret = httpd_resp_send(req, response, (ssize_t)strlen(response));
        if (send_ret != ESP_OK) {
            ESP_LOGW(TAG, "Upload response send failed: %s", esp_err_to_name(send_ret));
            return send_ret;
        }
        close_current_session(req);
        /* Return the device screen to the connection/instructions page after a
         * successful upload. Keeping the page in COMPLETE state left the panel
         * showing the saved file id and made the AP flow look stuck before the
         * next upload. */
        if (self) {
            notify_state(self, AP_SERVER_STATE_K_AP_STARTED, self->ap_ip);
        }
        return ESP_OK;
    }

    if (self) {
        notify_state(self, AP_SERVER_STATE_K_ERROR, "Save failed");
    }
    esp_err_t send_ret = httpd_resp_send(req, "{\"success\":false,\"error\":\"保存失败\"}", HTTPD_RESP_USE_STRLEN);
    close_current_session(req);
    return send_ret == ESP_OK ? ESP_OK : send_ret;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    ap_transfer_server_t *self = (ap_transfer_server_t *)req->user_ctx;
    const char           *mode = "ap";
    const char           *ip   = AP_IP;
    if (self != NULL) {
        mode = self->mode == AP_SERVER_MODE_LAN ? "lan" : "ap";
        ip   = self->ap_ip[0] != '\0' ? self->ap_ip : AP_IP;
    }
    char response[128];
    snprintf(response, sizeof(response), "{\"status\":\"ready\",\"mode\":\"%s\",\"ip\":\"%s\",\"url\":\"http://%s/\"}",
             mode, ip, ip);
    send_json(req, response);
    return ESP_OK;
}

static esp_err_t settings_handler(httpd_req_t *req)
{
    ap_transfer_server_t *self = (ap_transfer_server_t *)req->user_ctx;
    nvs_handle_t          nvs  = 0;
    bool                  nvs_open_ok =
        nvs_open(GALLERY_NAMESPACE, req->method == HTTP_POST ? NVS_READWRITE : NVS_READONLY, &nvs) == ESP_OK;
    int32_t interval = 5;
    if (nvs_open_ok) {
        nvs_get_i32(nvs, SLIDESHOW_INTERVAL_KEY, &interval);
    }
    bool close_service = false;
    bool stop_wifi     = false;
    bool enter_sleep   = false;

    if (req->method == HTTP_POST) {
        cJSON *root = read_json_body(req);
        if (!root) {
            if (nvs_open_ok) {
                nvs_close(nvs);
            }
            send_json(req, "{\"success\":false,\"error\":\"bad_json\"}");
            return ESP_FAIL;
        }
        cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "slideshow_interval");
        if (cJSON_IsNumber(item)) {
            interval = item->valueint;
            if (interval != 0 && interval != 5 && interval != 10 && interval != 30) {
                interval = 5;
            }
            nvs_set_i32(nvs, SLIDESHOW_INTERVAL_KEY, interval);
            nvs_commit(nvs);
            if (self && self->settings_changed_cb) {
                self->settings_changed_cb((int)interval, self->settings_changed_cb_ctx);
            }
            ESP_LOGI(TAG, "AP settings updated: slideshow_interval=%d", (int)interval);
        }
        cJSON *service_item = cJSON_GetObjectItemCaseSensitive(root, "service_enabled");
        if (cJSON_IsBool(service_item) && !cJSON_IsTrue(service_item)) {
            close_service = true;
        }
        cJSON *wifi_item = cJSON_GetObjectItemCaseSensitive(root, "wifi_enabled");
        if (cJSON_IsBool(wifi_item) && !cJSON_IsTrue(wifi_item)) {
            stop_wifi = true;
        }
        cJSON *sleep_item = cJSON_GetObjectItemCaseSensitive(root, "sleep");
        if (cJSON_IsBool(sleep_item) && cJSON_IsTrue(sleep_item)) {
            close_service = true;
            stop_wifi     = true;
            enter_sleep   = true;
        }
        if (close_service || stop_wifi || enter_sleep) {
            if (!is_authorized(req)) {
                cJSON_Delete(root);
                if (nvs_open_ok) {
                    nvs_close(nvs);
                }
                httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
                return ESP_FAIL;
            }
        }
        cJSON_Delete(root);
    }
    if (nvs_open_ok) {
        nvs_close(nvs);
    }

    const char *mode = "ap";
    const char *ip   = AP_IP;
    if (self != NULL) {
        mode = self->mode == AP_SERVER_MODE_LAN ? "lan" : "ap";
        ip   = self->ap_ip[0] != '\0' ? self->ap_ip : AP_IP;
    }
    char response[256];
    snprintf(response, sizeof(response),
             "{\"success\":true,\"slideshow_interval\":%d,\"service_running\":%s,"
             "\"mode\":\"%s\",\"ip\":\"%s\",\"url\":\"http://%s/\","
             "\"closing\":%s,\"sleep\":%s}",
             (int)interval, (self && self->running) ? "true" : "false", mode, ip, ip, close_service ? "true" : "false",
             enter_sleep ? "true" : "false");
    send_json(req, response);
    if (close_service || stop_wifi || enter_sleep) {
        ESP_LOGI(TAG, "Web control requested: close_service=%d stop_wifi=%d sleep=%d", close_service ? 1 : 0,
                 stop_wifi ? 1 : 0, enter_sleep ? 1 : 0);
        schedule_deferred_control(self, stop_wifi, enter_sleep);
    }
    return ESP_OK;
}

static esp_err_t photos_handler(httpd_req_t *req)
{
    cJSON *root   = cJSON_CreateObject();
    cJSON *photos = cJSON_CreateArray();
    if (!root || !photos) {
        if (root)
            cJSON_Delete(root);
        if (photos)
            cJSON_Delete(photos);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(root, "photos", photos);

    const int count = photo_get_count();
    for (int i = 0; i < count && i < PHOTO_MAX_PHOTOS; ++i) {
        photo_info_t info;
        memset(&info, 0, sizeof(info));
        if (photo_get_by_index(i, &info) != 0)
            continue;
        cJSON *item = cJSON_CreateObject();
        if (!item)
            continue;
        cJSON_AddStringToObject(item, "id", info.id);
        cJSON_AddStringToObject(item, "title", info.title);
        cJSON_AddStringToObject(item, "date", info.date);
        cJSON_AddStringToObject(item, "location", info.location);
        cJSON_AddStringToObject(item, "body", info.body);
        cJSON_AddNumberToObject(item, "width", info.width);
        cJSON_AddNumberToObject(item, "height", info.height);
        cJSON_AddNumberToObject(item, "size", info.file_size);
        cJSON_AddStringToObject(item, "format", info.file_size > AP_IMAGE_1BPP_SIZE ? "bwry2bpp" : "1bpp");
        cJSON_AddItemToArray(photos, item);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t ret = httpd_resp_sendstr(req, json);
    close_current_session(req);
    cJSON_free(json);
    return ret;
}

static esp_err_t photo_handler(httpd_req_t *req)
{
    ap_transfer_server_t *self = (ap_transfer_server_t *)req->user_ctx;

    char query[64] = {0};
    char id[16]    = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "id", id, sizeof(id)) != ESP_OK || id[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing id");
        return ESP_FAIL;
    }

    if (req->method == HTTP_DELETE) {
        if (!is_authorized(req)) {
            httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
            return ESP_FAIL;
        }
        const bool deleted = photo_delete(id) == 0;
        if (deleted && self && self->photos_changed_cb) {
            self->photos_changed_cb(self->photos_changed_cb_ctx);
        }
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Connection", "close");
        esp_err_t ret =
            httpd_resp_send(req, deleted ? "{\"success\":true}" : "{\"success\":false}", HTTPD_RESP_USE_STRLEN);
        close_current_session(req);
        if (ret != ESP_OK)
            return ret;
        return deleted ? ESP_OK : ESP_FAIL;
    }

    photo_info_t info;
    memset(&info, 0, sizeof(info));
    bool      found = false;
    const int count = photo_get_count();
    for (int i = 0; i < count && i < PHOTO_MAX_PHOTOS; ++i) {
        if (photo_get_by_index(i, &info) == 0 && strcmp(info.id, id) == 0) {
            found = true;
            break;
        }
    }
    if (!found || info.file_size == 0 || info.file_size > AP_IMAGE_2BPP_SIZE) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }

    uint8_t *buf = (uint8_t *)malloc(info.file_size);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }
    const int bytes = photo_load(id, buf, info.file_size);
    if (bytes <= 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t ret = httpd_resp_send(req, (const char *)buf, bytes);
    close_current_session(req);
    free(buf);
    return ret;
}

static esp_err_t photo_meta_handler(httpd_req_t *req)
{
    cJSON *root = read_json_body(req);
    if (!root) {
        send_json(req, "{\"success\":false,\"error\":\"bad_json\"}");
        return ESP_FAIL;
    }

    char id[16] = {0};
    copy_json_string(root, "id", id, sizeof(id));
    photo_info_t info;
    memset(&info, 0, sizeof(info));
    bool      found = false;
    const int count = photo_get_count();
    for (int i = 0; i < count && i < PHOTO_MAX_PHOTOS; ++i) {
        if (photo_get_by_index(i, &info) == 0 && strcmp(info.id, id) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        cJSON_Delete(root);
        send_json(req, "{\"success\":false,\"error\":\"not_found\"}");
        return ESP_FAIL;
    }

    copy_json_string(root, "title", info.title, sizeof(info.title));
    copy_json_string(root, "date", info.date, sizeof(info.date));
    copy_json_string(root, "location", info.location, sizeof(info.location));
    copy_json_string(root, "body", info.body, sizeof(info.body));
    cJSON_Delete(root);

    ap_transfer_server_t *self = (ap_transfer_server_t *)req->user_ctx;
    const bool            ok   = photo_update_info(id, &info) == 0;
    if (ok && self && self->photos_changed_cb) {
        self->photos_changed_cb(self->photos_changed_cb_ctx);
    }
    send_json(req, ok ? "{\"success\":true}" : "{\"success\":false}");
    return ok ? ESP_OK : ESP_FAIL;
}

static esp_err_t photo_move_handler(httpd_req_t *req)
{
    if (!is_authorized(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }
    cJSON *root = read_json_body(req);
    if (!root) {
        send_json(req, "{\"success\":false,\"error\":\"bad_json\"}");
        return ESP_FAIL;
    }
    char id[16] = {0};
    copy_json_string(root, "id", id, sizeof(id));
    cJSON    *delta_item = cJSON_GetObjectItemCaseSensitive(root, "delta");
    const int delta      = cJSON_IsNumber(delta_item) ? delta_item->valueint : 0;
    cJSON_Delete(root);

    ap_transfer_server_t *self = (ap_transfer_server_t *)req->user_ctx;
    const bool            ok   = photo_move(id, delta) == 0;
    if (ok && self && self->photos_changed_cb) {
        self->photos_changed_cb(self->photos_changed_cb_ctx);
    }
    send_json(req, ok ? "{\"success\":true}" : "{\"success\":false}");
    return ok ? ESP_OK : ESP_FAIL;
}

static esp_err_t photo_show_handler(httpd_req_t *req)
{
    cJSON *root = read_json_body(req);
    if (!root) {
        send_json(req, "{\"success\":false,\"error\":\"bad_json\"}");
        return ESP_FAIL;
    }
    char id[16] = {0};
    copy_json_string(root, "id", id, sizeof(id));
    cJSON_Delete(root);

    ap_transfer_server_t *self = (ap_transfer_server_t *)req->user_ctx;
    const bool            ok   = self && self->show_photo_cb && self->show_photo_cb(id, self->show_photo_cb_ctx);
    send_json(req, ok ? "{\"success\":true}" : "{\"success\":false,\"error\":\"not_found\"}");
    return ok ? ESP_OK : ESP_FAIL;
}

/* ------------------------------------------------------------------ */
/* HTTP server                                                         */
/* ------------------------------------------------------------------ */

static bool start_http_server(ap_transfer_server_t *self)
{
    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers  = 12;
    config.max_open_sockets  = 4;
    config.recv_wait_timeout = 30; /* Large images take time */
    config.send_wait_timeout = 10;
    config.lru_purge_enable  = true;

    esp_err_t err = httpd_start(&self->server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        self->server = NULL;
        return false;
    }

    /* Register handlers */
    httpd_uri_t index_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_handler,
        .user_ctx = self,
    };
    if (httpd_register_uri_handler(self->server, &index_uri) != ESP_OK)
        return false;

    httpd_uri_t upload_uri = {
        .uri      = "/upload",
        .method   = HTTP_POST,
        .handler  = upload_handler,
        .user_ctx = self,
    };
    if (httpd_register_uri_handler(self->server, &upload_uri) != ESP_OK)
        return false;

    httpd_uri_t status_uri = {
        .uri      = "/status",
        .method   = HTTP_GET,
        .handler  = status_handler,
        .user_ctx = self,
    };
    if (httpd_register_uri_handler(self->server, &status_uri) != ESP_OK)
        return false;

    httpd_uri_t settings_get_uri = {
        .uri      = "/settings",
        .method   = HTTP_GET,
        .handler  = settings_handler,
        .user_ctx = self,
    };
    if (httpd_register_uri_handler(self->server, &settings_get_uri) != ESP_OK)
        return false;

    httpd_uri_t settings_post_uri = {
        .uri      = "/settings",
        .method   = HTTP_POST,
        .handler  = settings_handler,
        .user_ctx = self,
    };
    if (httpd_register_uri_handler(self->server, &settings_post_uri) != ESP_OK)
        return false;

    httpd_uri_t photos_uri = {
        .uri      = "/photos",
        .method   = HTTP_GET,
        .handler  = photos_handler,
        .user_ctx = self,
    };
    if (httpd_register_uri_handler(self->server, &photos_uri) != ESP_OK)
        return false;

    httpd_uri_t photo_get_uri = {
        .uri      = "/photo",
        .method   = HTTP_GET,
        .handler  = photo_handler,
        .user_ctx = self,
    };
    if (httpd_register_uri_handler(self->server, &photo_get_uri) != ESP_OK)
        return false;

    httpd_uri_t photo_delete_uri = {
        .uri      = "/photo",
        .method   = HTTP_DELETE,
        .handler  = photo_handler,
        .user_ctx = self,
    };
    if (httpd_register_uri_handler(self->server, &photo_delete_uri) != ESP_OK)
        return false;

    httpd_uri_t photo_meta_uri = {
        .uri      = "/photo/meta",
        .method   = HTTP_POST,
        .handler  = photo_meta_handler,
        .user_ctx = self,
    };
    if (httpd_register_uri_handler(self->server, &photo_meta_uri) != ESP_OK)
        return false;

    httpd_uri_t photo_move_uri = {
        .uri      = "/photos/move",
        .method   = HTTP_POST,
        .handler  = photo_move_handler,
        .user_ctx = self,
    };
    if (httpd_register_uri_handler(self->server, &photo_move_uri) != ESP_OK)
        return false;

    httpd_uri_t photo_show_uri = {
        .uri      = "/photo/show",
        .method   = HTTP_POST,
        .handler  = photo_show_handler,
        .user_ctx = self,
    };
    if (httpd_register_uri_handler(self->server, &photo_show_uri) != ESP_OK)
        return false;

    ESP_LOGI(TAG, "HTTP server started at http://%s/", self->ap_ip[0] != '\0' ? self->ap_ip : AP_IP);
    return true;
}

/* ------------------------------------------------------------------ */
/* Access point                                                        */
/* ------------------------------------------------------------------ */

static bool start_access_point(ap_transfer_server_t *self)
{
    /* AP mode is entered from several states: normal STA, user-disabled WiFi,
     * and after long idle/sleep. Stop any stale WiFi activity first so the AP
     * beacon is backed by a fresh driver state. The C station manager
     * (main/wifi_manager.c) tolerates a transient reconnect attempt during AP
     * mode (esp_wifi_connect() from AP mode returns a tolerated error). */
    esp_err_t err = esp_wifi_scan_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_STATE) {
        ESP_LOGW(TAG, "esp_wifi_scan_stop before AP failed: %s", esp_err_to_name(err));
    }
    err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "esp_wifi_disconnect before AP failed: %s", esp_err_to_name(err));
    }
    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "esp_wifi_stop before AP failed: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    if (self->ap_netif) {
        esp_netif_destroy_default_wifi(self->ap_netif);
        self->ap_netif = NULL;
    }

    /* Create AP netif */
    self->ap_netif = esp_netif_create_default_wifi_ap();
    if (!self->ap_netif) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return false;
    }

    /* Configure IP: 192.168.4.1 */
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    err = esp_netif_dhcps_stop(self->ap_netif);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_netif_dhcps_stop failed: %s", esp_err_to_name(err));
    }
    err = esp_netif_set_ip_info(self->ap_netif, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_set_ip_info failed: %s", esp_err_to_name(err));
        goto fail;
    }
    err = esp_netif_dhcps_start(self->ap_netif);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_dhcps_start failed: %s", esp_err_to_name(err));
        goto fail;
    }

    /* WiFi AP config */
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    strcpy((char *)wifi_config.ap.ssid, AP_SSID);
    wifi_config.ap.ssid_len       = (uint8_t)strlen(AP_SSID);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.channel        = 1;
    strcpy((char *)wifi_config.ap.password, AP_PASSWORD);
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "Setting WiFi AP mode");
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(AP) failed: %s", esp_err_to_name(err));
        goto fail;
    }
    ESP_LOGI(TAG, "Setting WiFi AP config");
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(err));
        goto fail;
    }
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_ps(NONE) failed: %s", esp_err_to_name(err));
    }
    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(start_err));
        goto fail;
    }

    /* Keep the AP IP fixed and log the same address that the screen renders.
     * This avoids confusing users with any transient netif readback while
     * Wi-Fi mode is switching. */
    strncpy(self->ap_ip, AP_IP, sizeof(self->ap_ip) - 1);
    self->ap_ip[sizeof(self->ap_ip) - 1] = '\0';

    ESP_LOGI(TAG, "AP started: SSID=%s, IP=%s", AP_SSID, self->ap_ip);
    return true;

fail:
    if (self->ap_netif) {
        esp_netif_destroy_default_wifi(self->ap_netif);
        self->ap_netif = NULL;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void start_task(void *arg)
{
    ap_transfer_server_t *self = (ap_transfer_server_t *)arg;
    if (!self) {
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "AP start task running, stack watermark=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    vTaskDelay(pdMS_TO_TICKS(100));

    if (!self->starting) {
        ESP_LOGI(TAG, "AP start task cancelled before WiFi init");
        self->start_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (!start_access_point(self)) {
        self->running    = false;
        self->starting   = false;
        self->start_task = NULL;
        self->mode       = AP_SERVER_MODE_NONE;
        notify_state(self, AP_SERVER_STATE_K_ERROR, "AP start failed");
        vTaskDelete(NULL);
        return;
    }
    if (!self->starting) {
        ESP_LOGI(TAG, "AP start task cancelled after WiFi init");
        self->start_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (!start_http_server(self)) {
        self->running    = false;
        self->starting   = false;
        self->start_task = NULL;
        self->mode       = AP_SERVER_MODE_NONE;
        notify_state(self, AP_SERVER_STATE_K_ERROR, "HTTP start failed");
        vTaskDelete(NULL);
        return;
    }

    self->running    = true;
    self->starting   = false;
    self->start_task = NULL;
    notify_state(self, AP_SERVER_STATE_K_AP_STARTED, self->ap_ip);
    ESP_LOGI(TAG, "AP start task done, stack watermark=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    vTaskDelete(NULL);
}

void ap_transfer_server_init(ap_transfer_server_t *server)
{
    if (!server)
        return;
    memset(server, 0, sizeof(*server));
    ESP_LOGI(TAG, "ApTransferServer created");
}

void ap_transfer_server_start(ap_transfer_server_t *server)
{
    if (!server)
        return;
    if (server->running || server->starting) {
        ESP_LOGW(TAG, "Server already running");
        return;
    }

    ESP_LOGI(TAG, "Starting AP Transfer Server async");
    server->mode     = AP_SERVER_MODE_AP;
    server->starting = true;
    BaseType_t ok    = xTaskCreate(&start_task, "ap_transfer_start", 16384, server, 5, &server->start_task);
    if (ok != pdPASS) {
        server->starting   = false;
        server->start_task = NULL;
        server->mode       = AP_SERVER_MODE_NONE;
        ESP_LOGE(TAG, "Failed to create AP start task");
        notify_state(server, AP_SERVER_STATE_K_ERROR, "Start task failed");
    }
}

bool ap_transfer_server_start_lan(ap_transfer_server_t *server, const char *ip_address)
{
    if (!server)
        return false;
    if (server->running || server->starting) {
        ESP_LOGW(TAG, "Server already running");
        return true;
    }
    if (!ip_address || ip_address[0] == '\0') {
        ESP_LOGW(TAG, "LAN HTTP server start skipped: empty IP address");
        notify_state(server, AP_SERVER_STATE_K_ERROR, "No WiFi IP");
        return false;
    }

    server->mode = AP_SERVER_MODE_LAN;
    strncpy(server->ap_ip, ip_address, sizeof(server->ap_ip) - 1);
    server->ap_ip[sizeof(server->ap_ip) - 1] = '\0';
    ESP_LOGI(TAG, "Starting LAN HTTP server at http://%s/", server->ap_ip);
    if (!start_http_server(server)) {
        server->running = false;
        server->mode    = AP_SERVER_MODE_NONE;
        notify_state(server, AP_SERVER_STATE_K_ERROR, "HTTP start failed");
        return false;
    }
    server->running = true;
    notify_state(server, AP_SERVER_STATE_K_AP_STARTED, server->ap_ip);
    return true;
}

void ap_transfer_server_stop(ap_transfer_server_t *server)
{
    if (!server)
        return;
    if (!server->running && !server->starting && server->server == NULL && server->ap_netif == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Stopping AP Transfer Server");
    const ap_server_mode_t old_mode = server->mode;
    server->starting                = false;
    server->start_task              = NULL;

    if (server->server) {
        httpd_stop(server->server);
        server->server = NULL;
    }

    if (old_mode == AP_SERVER_MODE_AP) {
        ESP_LOGI(TAG, "Returning WiFi to STA mode");
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_set_mode(STA) failed: %s", esp_err_to_name(err));
        }
        err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
            ESP_LOGW(TAG, "esp_wifi_connect failed after AP stop: %s", esp_err_to_name(err));
        }
    }

    if (server->ap_netif) {
        esp_netif_destroy_default_wifi(server->ap_netif);
        server->ap_netif = NULL;
    }

    server->running = false;
    server->mode    = AP_SERVER_MODE_NONE;
    notify_state(server, AP_SERVER_STATE_K_STOPPED, "Server stopped");
}

bool ap_transfer_server_is_running(const ap_transfer_server_t *server)
{
    return server && server->running;
}

bool ap_transfer_server_is_ap_mode(const ap_transfer_server_t *server)
{
    return server && server->mode == AP_SERVER_MODE_AP;
}

bool ap_transfer_server_is_lan_mode(const ap_transfer_server_t *server)
{
    return server && server->mode == AP_SERVER_MODE_LAN;
}

ap_server_mode_t ap_transfer_server_get_mode(const ap_transfer_server_t *server)
{
    return server ? server->mode : AP_SERVER_MODE_NONE;
}

const char *ap_transfer_server_get_ap_ip(const ap_transfer_server_t *server)
{
    return server ? server->ap_ip : AP_IP;
}

/* ------------------------------------------------------------------ */
/* State notification                                                  */
/* ------------------------------------------------------------------ */

static void notify_state(ap_transfer_server_t *server, ap_server_state_t state, const char *message)
{
    if (!server)
        return;
    ESP_LOGI(TAG, "State: %d, message: %s", (int)state, message ? message : "");
    if (server->state_cb) {
        server->state_cb((int)state, message ? message : "", server->state_cb_ctx);
    }
}

/* ------------------------------------------------------------------ */
/* Callback setters                                                    */
/* ------------------------------------------------------------------ */

void ap_transfer_server_set_state_callback(ap_transfer_server_t *server, ap_server_state_cb_t cb, void *ctx)
{
    if (!server)
        return;
    server->state_cb     = cb;
    server->state_cb_ctx = ctx;
}

void ap_transfer_server_set_image_received_callback(ap_transfer_server_t *server, ap_server_image_received_cb_t cb,
                                                    void *ctx)
{
    if (!server)
        return;
    server->image_received_cb     = cb;
    server->image_received_cb_ctx = ctx;
}

void ap_transfer_server_set_settings_changed_callback(ap_transfer_server_t *server, ap_server_settings_changed_cb_t cb,
                                                      void *ctx)
{
    if (!server)
        return;
    server->settings_changed_cb     = cb;
    server->settings_changed_cb_ctx = ctx;
}

void ap_transfer_server_set_photos_changed_callback(ap_transfer_server_t *server, ap_server_photos_changed_cb_t cb,
                                                    void *ctx)
{
    if (!server)
        return;
    server->photos_changed_cb     = cb;
    server->photos_changed_cb_ctx = ctx;
}

void ap_transfer_server_set_show_photo_callback(ap_transfer_server_t *server, ap_server_show_photo_cb_t cb, void *ctx)
{
    if (!server)
        return;
    server->show_photo_cb     = cb;
    server->show_photo_cb_ctx = ctx;
}
