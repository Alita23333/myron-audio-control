import {exec} from './ksu.js';
const $=id=>document.getElementById(id);
const backend='/data/adb/modules/myron_audio_control/bin/audioctl';
const modeNames={stock:'原厂音效',default:'统一默认音效','per-app':'逐应用音效'};
let lastState=null,loaded=false,dirty=false,busy=false,rowId=0;
async function command(action,encoded=''){
  if(!['status','apply'].includes(action)||!(/^[A-Za-z0-9+/=]*$/).test(encoded))throw new Error('非法后端参数');
  const result=await exec(`${backend} ${action}${encoded?' '+encoded:''}`);
  if(result.errno!==0)throw new Error(result.stderr||result.stdout||'后端执行失败');
  return JSON.parse(result.stdout);
}
function notice(message,kind='info'){$('notice').textContent=message;$('notice').dataset.kind=kind;$('notice').hidden=false;}
function fail(error){notice(error.userMessage||'操作未完成，请重试。若持续失败，请查看高级诊断。','error');$('evidence').textContent=JSON.stringify({error:String(error.message),lastState},null,2);}
function userError(message){const error=new Error(message);error.userMessage=message;return error;}
function selectedMode(){return document.querySelector('input[name="mode"]:checked').value;}
function controls(){
  $('save').disabled=!loaded||busy||!dirty;$('save').textContent=busy?'正在保存…':'保存设置';
  $('draft').textContent=dirty?'有未保存的修改':'尚未修改设置';
  $('settings').querySelectorAll('input,button:not(#save)').forEach(el=>{el.disabled=!loaded||busy;});$('refresh').disabled=busy;
}
function changed(){dirty=true;$('notice').hidden=true;controls();}
function modeChanged(){$('perApp').hidden=selectedMode()!=='per-app';}
function countRows(){const rows=[...$('rows').children];$('ruleCount').textContent=`${rows.length} 个应用`;rows.forEach((row,index)=>{row.querySelector('.row-heading span').textContent=`应用 ${index+1}`;});}
function addRow(packageName='',scene='default'){
  const row=document.createElement('div');row.className='row';const id=++rowId;
  const heading=document.createElement('div');heading.className='row-heading';const title=document.createElement('span');
  const remove=document.createElement('button');remove.type='button';remove.className='remove';remove.textContent='删除';remove.setAttribute('aria-label','删除这条应用规则');remove.onclick=()=>{row.remove();countRows();changed();};heading.append(title,remove);
  const input=document.createElement('input');input.type='text';input.placeholder='例如 tv.danmaku.bili';input.value=packageName;input.autocapitalize='none';input.spellcheck=false;input.maxLength=200;input.setAttribute('aria-label','应用包名');
  const scenes=document.createElement('fieldset');scenes.className='scene-list';const legend=document.createElement('legend');legend.className='sr-only';legend.textContent='选择该应用的音效场景';scenes.append(legend);
  for(const [value,text] of [['default','默认音效'],['long_video','长视频场景'],['short_video','短视频场景'],['navigation','导航场景']]){
    const label=document.createElement('label');label.className='scene-option';const radio=document.createElement('input');radio.type='radio';radio.name=`scene-${id}`;radio.value=value;radio.checked=scene===value;const caption=document.createElement('span');caption.textContent=text;label.append(radio,caption);scenes.append(label);
  }
  row.append(heading,input,scenes);$('rows').append(row);countRows();return input;
}
function fill(config){
  $('unity').checked=config.volumeUnity;document.querySelectorAll('input[name="mode"]').forEach(el=>{el.checked=el.value===config.effectMode;});
  $('rows').replaceChildren();for(const [pkg,scene] of Object.entries(config.appScenes))addRow(pkg,scene);countRows();modeChanged();
}
function renderStatus(state){
  const bootMissing=state.bootReady!==true;
  const badge=$('badge');badge.dataset.state=state.disabled?'disabled':state.rebootRequired||bootMissing?'pending':'ready';badge.textContent=state.disabled?'已禁用':state.rebootRequired?'等待重启':bootMissing?'需检查启动':'配置已载入';
  $('status').textContent=state.disabled?'模块已禁用。若尚未重启，请重启以恢复原厂。':state.rebootRequired?'设置已保存，重启手机后应用新设置。':bootMissing?'尚未确认本次开机加载成功。请重启；若仍异常，查看高级诊断。':'本次开机已加载配置。实际效果取决于应用的播放路径。';
  $('activeSummary').replaceChildren();if(state.active&&!state.disabled&&!bootMissing){for(const text of [`音量衰减：${state.active.volumeUnity?'解除':'原厂'}`,`音效：${modeNames[state.active.effectMode]||'未知'}`]){const chip=document.createElement('span');chip.textContent=text;$('activeSummary').append(chip);}}
  $('scope').textContent=state.activeMeta?.scope==='all'?'覆盖范围：全部可注入应用。':'覆盖范围：小范围测试；升级会保留原有范围设置。';$('evidence').textContent=JSON.stringify(state,null,2);
}
async function refresh(forceFill=false){
  try{const state=await command('status');if(!state.pending)throw new Error('后端未初始化');lastState=state;renderStatus(state);if(forceFill||!dirty){fill(state.pending);dirty=false;}loaded=true;controls();return true;}
  catch(error){fail(error);controls();return false;}
}
async function save(event){
  event.preventDefault();if(!loaded||busy||!dirty)return;let config;
  try{
    const appScenes=Object.create(null);for(const row of $('rows').children){const pkg=row.querySelector('input[type=text]').value.trim();
      if(!/^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+$/.test(pkg)||pkg.length>200){row.querySelector('input[type=text]').focus();throw userError('请填写完整的应用包名，例如 tv.danmaku.bili。');}
      if(Object.hasOwn(appScenes,pkg))throw userError('同一个应用只能添加一条规则，请删除重复项。');appScenes[pkg]=row.querySelector('input[type=radio]:checked').value;
    }
    if(Object.keys(appScenes).length>256)throw userError('最多可保存 256 条应用规则。');config={version:1,volumeUnity:$('unity').checked,effectMode:selectedMode(),appScenes};
  }catch(error){fail(error);return;}
  busy=true;controls();
  try{
    const bytes=new TextEncoder().encode(JSON.stringify(config));const encoded=btoa(Array.from(bytes,b=>String.fromCharCode(b)).join(''));await command('apply',encoded);
    // Refresh failure must not turn a successful save into a failed save.
    dirty=false;if(await refresh(true))notice('设置已保存。请完整重启手机，让新设置生效。');else notice('设置已保存，但暂时无法刷新状态。请重启手机后重新打开页面。');
  }catch(error){fail(error);}finally{busy=false;controls();}
}
$('settings').addEventListener('change',()=>{modeChanged();changed();});$('settings').addEventListener('input',changed);
$('add').onclick=()=>{const input=addRow();changed();input.focus();};$('settings').onsubmit=save;$('refresh').onclick=()=>refresh();controls();refresh();
