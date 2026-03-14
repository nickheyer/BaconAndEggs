let status = {};

function $(id) { return document.getElementById(id); }

function togglePanel(id) {
  $(id).classList.toggle('hidden');
}

async function api(endpoint, body) {
  const opts = body ? {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)} : {};
  const r = await fetch('/api/' + endpoint, opts);
  return r.json();
}

function formatUptime(s) {
  const d = Math.floor(s/86400), h = Math.floor((s%86400)/3600), m = Math.floor((s%3600)/60);
  return (d ? d+'d ' : '') + h+'h ' + m+'m';
}

function renderServers(data) {
  const grid = $('server-grid');
  grid.innerHTML = '';
  if (!data.servers || data.servers.length === 0) {
    grid.innerHTML = '<div style="color:#888;padding:8px">No servers configured</div>';
    return;
  }
  data.servers.forEach(s => {
    const stateClass = 'state-' + s.state;
    const stateLabel = s.state.toUpperCase();
    let detail = '';
    if (s.state === 'waking') detail = ' (attempt ' + s.wol_count + ')';
    grid.innerHTML += `<div class="card">
      <h3>${s.name}</h3>
      <div class="mac">${s.mac}</div>
      ${s.ip ? '<div class="ip">' + s.ip + '</div>' : ''}
      <div class="state ${stateClass}">${stateLabel}${detail}</div>
      <div class="actions">
        <button class="btn btn-sm" onclick="wake('${s.name}')">Wake</button>
        <button class="btn btn-sm btn-danger" onclick="removeServer('${s.name}')">Remove</button>
      </div>
    </div>`;
  });
}

async function refresh() {
  try {
    status = await api('status');
    $('uptime').textContent = 'Uptime: ' + formatUptime(status.uptime);
    $('ntp').textContent = 'NTP: ' + (status.ntp_synced ? 'synced' : 'pending');
    renderServers(status);
    $('autowake-btn').textContent = status.autowake ? 'ON' : 'OFF';
    $('autowake-btn').className = 'btn btn-sm ' + (status.autowake ? 'btn-accent' : 'btn-secondary');
    updateTargetSelect();
  } catch(e) { console.error('refresh failed', e); }
}

function updateTargetSelect() {
  const sel = $('sched-target');
  const cur = sel.value;
  sel.innerHTML = '<option value="all">All</option>';
  if (status.servers) {
    status.servers.forEach(s => {
      sel.innerHTML += `<option value="${s.name}">${s.name}</option>`;
    });
  }
  sel.value = cur || 'all';
}

async function wake(name) { await api('wake', {name}); setTimeout(refresh, 500); }
async function wakeAll() { await api('wake', {all:true}); setTimeout(refresh, 500); }

async function removeServer(name) {
  if (!confirm('Remove ' + name + '?')) return;
  await api('server/remove', {name});
  refresh();
}

async function addServer(e) {
  e.preventDefault();
  const body = {name:$('add-name').value, mac:$('add-mac').value};
  if ($('add-ip').value) body.ip = $('add-ip').value;
  const r = await api('server/add', body);
  if (r.ok) {
    $('add-name').value = '';
    $('add-mac').value = '';
    $('add-ip').value = '';
    refresh();
  } else {
    alert(r.error);
  }
}

async function discoverMac() {
  const ip = $('add-ip').value;
  if (!ip) { alert('Enter an IP first'); return; }
  await api('discover', {ip});
  // Poll for result
  const btn = document.querySelector('[onclick="discoverMac()"]');
  btn.textContent = 'Searching...';
  btn.disabled = true;
  let attempts = 0;
  const poll = setInterval(async () => {
    const r = await api('discover');
    if (r.state === 'found') {
      clearInterval(poll);
      $('add-mac').value = r.mac;
      btn.textContent = 'Discover MAC';
      btn.disabled = false;
    } else if (r.state !== 'pending' || ++attempts > 10) {
      clearInterval(poll);
      btn.textContent = 'Discover MAC';
      btn.disabled = false;
      if (r.state !== 'found') alert('MAC not found for ' + ip);
    }
  }, 1000);
}

async function toggleAutowake() {
  await api('autowake', {enabled: !status.autowake});
  refresh();
}

async function setTimezone() {
  const offset = parseInt($('tz-offset').value);
  if (isNaN(offset)) return;
  await api('timezone', {offset});
}

// Schedules
async function loadSchedules() {
  const data = await api('schedules');
  const list = $('sched-list');
  list.innerHTML = '';
  $('tz-offset').value = data.utc_offset;
  if (!data.schedules || data.schedules.length === 0) {
    list.innerHTML = '<div style="color:#888;font-size:.85em">No schedules</div>';
    return;
  }
  const dayNames = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
  data.schedules.forEach(s => {
    let days = '';
    if (s.day_mask === 127) days = 'Daily';
    else if (s.day_mask === 62) days = 'Weekdays';
    else if (s.day_mask === 65) days = 'Weekends';
    else {
      for (let i = 0; i < 7; i++) {
        if (s.day_mask & (1 << i)) days += dayNames[i] + ' ';
      }
    }
    const time = String(s.hour).padStart(2,'0') + ':' + String(s.minute).padStart(2,'0');
    list.innerHTML += `<div class="sched-item">
      <span>${days} ${time} → ${s.target}</span>
      <button class="btn btn-sm btn-danger" onclick="removeSchedule(${s.index})">X</button>
    </div>`;
  });
}

async function addSchedule(e) {
  e.preventDefault();
  const [h, m] = $('sched-time').value.split(':').map(Number);
  await api('schedule/add', {
    days: parseInt($('sched-days').value),
    hour: h, min: m,
    target: $('sched-target').value
  });
  loadSchedules();
}

async function removeSchedule(idx) {
  await api('schedule/remove', {index: idx});
  loadSchedules();
}

// MQTT
async function loadMqtt() {
  const data = await api('mqtt');
  $('mqtt-enabled').checked = data.enabled;
  $('mqtt-host').value = data.host;
  $('mqtt-port').value = data.port;
  $('mqtt-user').value = data.user;
  $('mqtt-status').textContent = data.connected ? 'Connected' : 'Disconnected';
  $('mqtt-status').style.color = data.connected ? '#00b894' : '#636e72';
}

async function saveMqtt(e) {
  e.preventDefault();
  await api('mqtt', {
    enabled: $('mqtt-enabled').checked,
    host: $('mqtt-host').value,
    port: parseInt($('mqtt-port').value),
    user: $('mqtt-user').value,
    pass: $('mqtt-pass').value
  });
  loadMqtt();
}

// Webhook
async function loadWebhook() {
  const data = await api('webhook');
  $('wh-enabled').checked = data.enabled;
  $('wh-url').value = data.url;
}

async function saveWebhook(e) {
  e.preventDefault();
  await api('webhook', {
    enabled: $('wh-enabled').checked,
    url: $('wh-url').value
  });
}

// Init
refresh();
loadSchedules();
loadMqtt();
loadWebhook();
setInterval(refresh, 5000);
