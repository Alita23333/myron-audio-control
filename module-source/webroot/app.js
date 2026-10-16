import {exec} from './ksu.js';
const $=id=>document.getElementById(id);
const backend='/data/adb/modules/myron_audio_control/bin/audioctl';
const modeNames={stock:'原厂音效',default:'统一默认音效','per-app':'逐应用音效'};
let lastState=null,loaded=false,dirty=false,busy=false,rowId=0,apps=null,appsLoading=false;
async function command(action,encoded=''){
  if(!['status','apply','apps'].includes(action)||!(/^[A-Za-z0-9+/=]*$/).test(encoded))throw new Error('非法后端参数');
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
  const selected=new Set([...$('rows').children].map(row=>row.querySelector('input[type=text]').value));
  $('settings').querySelectorAll('input,button:not(#save)').forEach(el=>{el.disabled=!loaded||busy||(el.classList.contains('app-result')&&selected.has(el.dataset.packageName))||(el.id==='reloadApps'&&appsLoading);});$('refresh').disabled=busy;
}
function changed(){dirty=true;$('notice').hidden=true;controls();}
function modeChanged(){$('perApp').hidden=selectedMode()!=='per-app';}
function countRows(){const rows=[...$('rows').children];$('ruleCount').textContent=`${rows.length} 个应用`;rows.forEach(row=>{const pkg=row.querySelector('input[type=text]').value;row.querySelector('.row-heading span').textContent=apps?.find(app=>app.packageName===pkg)?.label||pkg;});}
function addRow(packageName='',scene='default'){
  const row=document.createElement('div');row.className='row';const id=++rowId;
  const heading=document.createElement('div');heading.className='row-heading';const title=document.createElement('span');
  const remove=document.createElement('button');remove.type='button';remove.className='remove';remove.textContent='删除';remove.setAttribute('aria-label','删除这条应用规则');remove.onclick=()=>{row.remove();countRows();changed();};heading.append(title,remove);
  const input=document.createElement('input');input.type='text';input.value=packageName;input.readOnly=true;input.setAttribute('aria-label','应用包名（自动填写）');
  const scenes=document.createElement('fieldset');scenes.className='scene-list';const legend=document.createElement('legend');legend.className='sr-only';legend.textContent='选择该应用的音效场景';scenes.append(legend);
  for(const [value,text] of [['default','默认音效'],['long_video','长视频场景'],['short_video','短视频场景'],['navigation','导航场景']]){
    const label=document.createElement('label');label.className='scene-option';const radio=document.createElement('input');radio.type='radio';radio.name=`scene-${id}`;radio.value=value;radio.checked=scene===value;const caption=document.createElement('span');caption.textContent=text;label.append(radio,caption);scenes.append(label);
  }
  row.append(heading,input,scenes);$('rows').append(row);countRows();return input;
}
function renderApps(){
  $('appResults').replaceChildren();if(!apps)return;
  const query=$('appSearch').value.trim().toLocaleLowerCase();
  const selected=new Set([...$('rows').children].map(row=>row.querySelector('input[type=text]').value));
  const matches=apps.filter(app=>(!app.system||$('showSystem').checked)&&(app.label.toLocaleLowerCase().includes(query)||app.packageName.toLowerCase().includes(query)));
  $('appListStatus').textContent=matches.length?`${matches.length} 个应用 · 点击添加，已添加的应用不可重复选择`:'没有匹配的应用，可更换关键词或显示系统应用。';
  for(const app of matches){
    const button=document.createElement('button');button.type='button';button.className='app-result';button.dataset.packageName=app.packageName;button.disabled=busy||selected.has(app.packageName);
    const name=document.createElement('strong');name.textContent=app.label;
    const pkg=document.createElement('small');pkg.textContent=app.packageName;
    const state=document.createElement('span');state.className='hint';state.textContent=selected.has(app.packageName)?'已添加':app.system?'系统应用 · 添加':'添加';
    button.append(name,pkg,state);button.onclick=()=>{if(busy||[...$('rows').children].some(row=>row.querySelector('input[type=text]').value===app.packageName))return;if($('rows').children.length>=256){notice('最多可保存 256 条应用规则。','error');return;}const input=addRow(app.packageName);changed();$('appPicker').hidden=true;input.closest('.row').scrollIntoView({block:'nearest',behavior:'auto'});};$('appResults').append(button);
  }
}
async function loadApps(){
  if(appsLoading)return;appsLoading=true;$('reloadApps').disabled=true;$('appListStatus').textContent='正在读取本机应用…';
  try{const result=await command('apps');if(!result.ok||!Array.isArray(result.apps)||result.apps.some(app=>typeof app.packageName!=='string'||typeof app.label!=='string'||typeof app.system!=='boolean'))throw new Error('应用列表格式错误');
    apps=result.apps.sort((a,b)=>a.label.localeCompare(b.label,'zh-CN')||a.packageName.localeCompare(b.packageName));countRows();renderApps();
  }catch(error){$('appListStatus').textContent='无法读取应用列表，请解锁手机后重试。已有规则不会丢失。';$('appResults').replaceChildren();}
  finally{appsLoading=false;$('reloadApps').disabled=false;}
}
function fill(config){
  $('unity').checked=config.volumeUnity;document.querySelectorAll('input[name="mode"]').forEach(el=>{el.checked=el.value===config.effectMode;});
  document.querySelectorAll('input[name="bass"]').forEach(el=>{el.checked=Number(el.value)===(config.bassBoost??0);});
  $('rows').replaceChildren();for(const [pkg,scene] of Object.entries(config.appScenes))addRow(pkg,scene);countRows();modeChanged();
}
function renderStatus(state){
  const bootMissing=state.bootReady!==true;
  const badge=$('badge');badge.dataset.state=state.disabled?'disabled':state.rebootRequired||bootMissing?'pending':'ready';badge.textContent=state.disabled?'已禁用':state.rebootRequired?'等待重启':bootMissing?'需检查启动':'配置已载入';
  const nativeLog=state.nativeReadLog||'';
  const readSeen=/\bread_override process=/.test(nativeLog);
  const failed=state.active?.volumeUnity&&/\b(?:hook_failed|read_override_failed|not_covered) process=/.test(nativeLog);
  if(!state.disabled&&!state.rebootRequired&&!bootMissing)badge.textContent=failed?'部分应用需检查':readSeen?'已记录应用读取':'服务侧已载入';
  $('status').textContent=state.disabled?'模块已禁用。若尚未重启，请重启以恢复原厂。':state.rebootRequired?'设置已保存，重启手机后应用新设置。':bootMissing?'尚未确认本次开机加载成功。请重启；若仍异常，查看高级诊断。':failed?'配置已挂载，但日志中存在未覆盖或读取失败的应用。请查看高级诊断；这不代表所有应用均失效。':readSeen?'配置已挂载，日志记录到应用实际读取。记录不代表所有应用或当前播放均已验证。':state.active?.volumeUnity?'服务侧配置已挂载，尚无应用实际读取记录。请播放后刷新；仅凭此状态不能确认已解除音量衰减。':'服务侧配置已载入，应用名单音量保持原厂。';
  $('activeSummary').replaceChildren();if(state.active&&!state.disabled&&!bootMissing){for(const text of [`音量衰减：${state.active.volumeUnity?'解除':'原厂'}`,`音效：${modeNames[state.active.effectMode]||'未知'}`]){const chip=document.createElement('span');chip.textContent=text;$('activeSummary').append(chip);}}
  $('scope').textContent=state.activeMeta?.scope==='all'?'覆盖范围：全部可注入应用。':'覆盖范围：小范围测试；升级会保留原有范围设置。';$('evidence').textContent=JSON.stringify(state,null,2);
  const bassNames=['原厂','轻度增强','中度增强','较强增强'];
  const bassStates={stock:'保持原厂',active:'增强已写入并核对',idle:'等待扬声器播放',other_route:'其他输出路径，暂停增强',communication_or_mode_unknown:'通话或模式未确认，暂停增强',gain_changed:'功放状态变化，暂停增强',system_owned:'系统已接管，等待下次完整启动外放',nonfactory_attenuation:'当前不是已验证的原厂衰减，未覆盖',unsupported_hardware:'功放或路由不符合已验证条件',error:'控制异常，请查看诊断',error_restored:'控制异常，已释放并恢复本模块持有的设置',restore_failed:'恢复未确认，请关闭模块并重启',watchdog_restored:'后台退出，已执行回退',service_stopped_check_restore:'后台已停止，需核对恢复状态',stale:'记录来自上次开机',stopped_restored:'已停止并释放设置',not_started:'尚未启动'};
  const activeBass=state.active?.bassBoost??0;
  $('bassStatus').textContent=state.disabled?'模块已禁用；重启可完整恢复。':`本次开机选择：${bassNames[activeBass]||'未知'}。${bassStates[state.bass?.state||'not_started']||'状态未确认'}。`;
}
async function refresh(forceFill=false){
  try{const state=await command('status');if(!state.pending)throw new Error('后端未初始化');lastState=state;renderStatus(state);if(forceFill||!dirty){fill(state.pending);dirty=false;}loaded=true;controls();return true;}
  catch(error){fail(error);controls();return false;}
}
async function save(event){
  event.preventDefault();if(!loaded||busy||!dirty)return;let config;
  try{
    const appScenes=Object.create(null);for(const row of $('rows').children){const pkg=row.querySelector('input[type=text]').value.trim();
      if(!/^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+$/.test(pkg)||pkg.length>200){throw userError('这条应用规则无效，请删除后从应用列表重新添加。');}
      if(Object.hasOwn(appScenes,pkg))throw userError('同一个应用只能添加一条规则，请删除重复项。');appScenes[pkg]=row.querySelector('input[type=radio]:checked').value;
    }
    if(Object.keys(appScenes).length>256)throw userError('最多可保存 256 条应用规则。');config={version:1,volumeUnity:$('unity').checked,effectMode:selectedMode(),appScenes,bassBoost:Number(document.querySelector('input[name="bass"]:checked').value)};
  }catch(error){fail(error);return;}
  busy=true;controls();
  try{
    const bytes=new TextEncoder().encode(JSON.stringify(config));const encoded=btoa(Array.from(bytes,b=>String.fromCharCode(b)).join(''));await command('apply',encoded);
    // Refresh failure must not turn a successful save into a failed save.
    dirty=false;if(await refresh(true))notice('设置已保存。请完整重启手机，让新设置生效。');else notice('设置已保存，但暂时无法刷新状态。请重启手机后重新打开页面。');
  }catch(error){fail(error);}finally{busy=false;controls();}
}
$('settings').addEventListener('change',event=>{if(event.target.closest('#appPicker'))return;modeChanged();changed();});$('settings').addEventListener('input',event=>{if(!event.target.closest('#appPicker'))changed();});
$('add').onclick=()=>{$('appPicker').hidden=false;$('appSearch').focus();if(apps)renderApps();else loadApps();};
$('closeApps').onclick=()=>{$('appPicker').hidden=true;$('add').focus();};$('reloadApps').onclick=loadApps;$('appSearch').oninput=renderApps;$('showSystem').onchange=renderApps;
$('appSearch').onkeydown=event=>{if(event.key==='Enter')event.preventDefault();};
$('settings').onsubmit=save;$('refresh').onclick=()=>refresh();controls();refresh();
