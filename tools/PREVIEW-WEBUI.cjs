// Local UI-only fixture. Never sends commands to the phone or runs a shell.
const http=require('http'),fs=require('fs'),path=require('path');
const root=path.resolve(__dirname,'../module-source/webroot');
const mock=`let config={version:1,volumeUnity:true,effectMode:'stock',appScenes:{}};
const active=structuredClone(config);
export async function exec(command){
 await new Promise(resolve=>setTimeout(resolve,150));
 const match=command.match(/^\\/data\\/adb\\/modules\\/myron_audio_control\\/bin\\/audioctl (status|apply)(?: ([A-Za-z0-9+/=]+))?$/);
 if(!match)return {errno:1,stderr:'unexpected command',stdout:''};
 if(match[1]==='apply'){config=JSON.parse(atob(match[2]));return {errno:0,stderr:'',stdout:JSON.stringify({ok:true,rebootRequired:true})};}
 return {errno:0,stderr:'',stdout:JSON.stringify({ok:true,active,pending:config,activeMeta:{scope:'all'},disabled:false,bootReady:true,rebootRequired:JSON.stringify(active)!==JSON.stringify(config),bootLog:'UI preview only — not device verification',nativeReadLog:''})};
}`;
http.createServer((req,res)=>{
 const url=new URL(req.url,'http://127.0.0.1');const name=url.pathname;
 const fixture=['savefail','disabled','bootfail'].includes(url.searchParams.get('fixture'))?url.searchParams.get('fixture'):'';
 if(name==='/ksu.js'){
  let script=mock;
  if(fixture==='savefail')script=script.replace("if(match[1]==='apply'){config", "if(match[1]==='apply')return {errno:1,stderr:'preview save failure',stdout:''}; if(match[1]==='apply'){config");
  if(fixture==='disabled')script=script.replace('disabled:false,bootReady:true','disabled:true,bootReady:false');
  if(fixture==='bootfail')script=script.replace('bootReady:true','bootReady:false');
  res.setHeader('Content-Type','text/javascript; charset=utf-8');res.end(script);return;
 }
 const files={'/':'index.html','/index.html':'index.html','/app.js':'app.js','/style.css':'style.css'};
 if(!files[name]){res.writeHead(404);res.end();return;}
 let contents=fs.readFileSync(path.join(root,files[name]),'utf8');
 if(fixture&&files[name]==='index.html')contents=contents.replace('src="app.js"',`src="app.js?fixture=${fixture}"`);
 if(fixture&&files[name]==='app.js')contents=contents.replace("'./ksu.js'",`'./ksu.js?fixture=${fixture}'`);
 res.setHeader('Content-Type',name.endsWith('.css')?'text/css':name.endsWith('.js')?'text/javascript':'text/html; charset=utf-8');res.end(contents);
}).listen(8765,'127.0.0.1',()=>console.log('UI-only preview: http://127.0.0.1:8765'));
