// Project-X :: Minimal Window Controller

const RVA_REGISTRY = [
  { name: 'rva::print', rva: 0x1cab4b0 },
  { name: 'rva::task_defer', rva: 0x4319b60 },
  { name: 'rva::script_context_resume', rva: 0x4260f50 },
  { name: 'rva::task_scheduler', rva: 0x8b5cee8 },
  { name: 'rva::push_instance', rva: 0x41a5f20 },
  { name: 'rva::get_capabilities', rva: 0x1ce6760 },
  { name: 'rva::find_property_map', rva: 0x1ce4be0 },
  { name: 'rva::property_ktable', rva: 0x80bbe20 },
  { name: 'rva::shared_string_create', rva: 0x4925190 },
  { name: 'rva::shared_string_release', rva: 0xa47230 },
  { name: 'rva::fflag_registry', rva: 0x887ca40 },
  { name: 'rva::udatadirect_indexf', rva: 0x41a3d10 },
  { name: 'rva::udatadirect_newindexf', rva: 0x41a4bf0 },
  { name: 'rva::instance_index', rva: 0x41a1eb0 },
  { name: 'rva::instance_newindex', rva: 0x41a1710 },
  { name: 'rva::lua_newuserdatatagged', rva: 0x26fa460 },
  { name: 'rva_vm::luaD_throw', rva: 0x2709720 },
  { name: 'rva_vm::luaD_callint', rva: 0x26f9410 },
  { name: 'rva_vm::lua_vm_load', rva: 0x41b3b50 },
  { name: 'rva_vm::luau_execute', rva: 0x2736e10 },
  { name: 'rva_vm::luaT_objtypename', rva: 0x2725290 },
  { name: 'rva_flag::LuauCIProto', rva: 0x7C51288 },
  { name: 'rva_flag::LuauCallFeedback', rva: 0x7C51248 },
  { name: 'rva_flag::DebugLuauUserDefinedClassesRuntime', rva: 0x7C512C8 },
  { name: 'rva_flag::LuauFastpcall', rva: 0x7C51308 }
];

let baseAddress = 0x7FF700000000n;

function rebase(rva) {
  const val = baseAddress + BigInt(rva);
  return '0x' + val.toString(16).toUpperCase();
}

function hex(val) {
  return '0x' + val.toString(16).toUpperCase();
}

function renderTable(filter = '') {
  const tbody = document.getElementById('rva-rows');
  tbody.innerHTML = '';
  const q = filter.toLowerCase();
  
  RVA_REGISTRY.filter(item => item.name.toLowerCase().includes(q) || hex(item.rva).toLowerCase().includes(q))
    .forEach(item => {
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td>${item.name}</td>
        <td>${hex(item.rva)}</td>
        <td>${rebase(item.rva)}</td>
      `;
      tbody.appendChild(tr);
    });
}

function log(msg) {
  const out = document.getElementById('console-output');
  const d = document.createElement('div');
  const now = new Date();
  const ts = `[${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}:${String(now.getSeconds()).padStart(2, '0')}]`;
  d.innerHTML = `<span class="ts">${ts}</span> ${msg}`;
  out.appendChild(d);
  out.scrollTop = out.scrollHeight;
}

// Window Dragging Logic
function initWindowDrag() {
  const win = document.getElementById('app-window');
  const titlebar = document.getElementById('window-titlebar');
  let isDragging = false;
  let offsetX = 0;
  let offsetY = 0;

  titlebar.addEventListener('mousedown', (e) => {
    if (e.target.closest('.win-btn')) return;
    isDragging = true;
    const rect = win.getBoundingClientRect();
    offsetX = e.clientX - rect.left;
    offsetY = e.clientY - rect.top;
    win.style.transform = 'none';
    win.style.left = `${rect.left}px`;
    win.style.top = `${rect.top}px`;
  });

  window.addEventListener('mousemove', (e) => {
    if (!isDragging) return;
    win.style.left = `${Math.max(0, e.clientX - offsetX)}px`;
    win.style.top = `${Math.max(0, e.clientY - offsetY)}px`;
  });

  window.addEventListener('mouseup', () => {
    isDragging = false;
  });
}

// Window Controls
function initWindowControls() {
  const win = document.getElementById('app-window');
  const btnMin = document.getElementById('btn-min');
  const btnClose = document.getElementById('btn-close');
  let isMin = false;

  btnMin.addEventListener('click', () => {
    const content = win.querySelector('.window-content');
    const tabs = win.querySelector('.win-tabs');
    const footer = win.querySelector('.win-footer');
    isMin = !isMin;
    content.style.display = isMin ? 'none' : 'flex';
    tabs.style.display = isMin ? 'none' : 'flex';
    footer.style.display = isMin ? 'none' : 'flex';
    win.style.height = isMin ? '32px' : '440px';
  });

  btnClose.addEventListener('click', () => {
    win.style.display = 'none';
    setTimeout(() => {
      win.style.display = 'flex';
      log('window restored');
    }, 1200);
  });
}

// Tab Switching
function initTabs() {
  const tabs = document.querySelectorAll('.tab-btn');
  tabs.forEach(btn => {
    btn.addEventListener('click', () => {
      tabs.forEach(t => t.classList.remove('active'));
      document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
      btn.classList.add('active');
      const target = btn.getAttribute('data-tab');
      document.getElementById(target).classList.add('active');
    });
  });
}

document.addEventListener('DOMContentLoaded', () => {
  renderTable();
  initWindowDrag();
  initWindowControls();
  initTabs();

  const editor = document.getElementById('code-box');
  const statusLine = document.getElementById('status-line');
  const statBytes = document.getElementById('stat-bytes');

  editor.addEventListener('input', () => {
    statBytes.textContent = `${new TextEncoder().encode(editor.value).length} B`;
  });

  document.getElementById('btn-clear-code').addEventListener('click', () => {
    editor.value = '';
    statBytes.textContent = '0 B';
  });

  document.getElementById('btn-attach').addEventListener('click', () => {
    statusLine.textContent = 'ATTACHING...';
    statusLine.style.color = '#eab308';
    log('scanning for engine process...');
    setTimeout(() => {
      statusLine.textContent = 'ATTACHED [OK]';
      statusLine.style.color = '#22c55e';
      log('process handle acquired (pid: 14208)');
    }, 300);
  });

  document.getElementById('btn-run').addEventListener('click', () => {
    const code = editor.value.trim();
    if (!code) return;
    statusLine.textContent = 'DISPATCHING...';
    statusLine.style.color = '#fff';
    log(`staging payload (${code.length} bytes) to 0x0000021A4B010000`);
    
    setTimeout(() => {
      statusLine.textContent = 'EXECUTED [0x0]';
      statusLine.style.color = '#22c55e';
      log('runtime execution completed via TaskScheduler cycle');
    }, 250);
  });

  document.getElementById('rva-search').addEventListener('input', (e) => {
    renderTable(e.target.value);
  });
});
