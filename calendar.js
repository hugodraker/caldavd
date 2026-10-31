<!--  ============================================================================ -->
//    CalDAV Server & HTTP Web Server - Front End GUI
 
//    2026 HUGO
  
//    THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
//    AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
<!--  ============================================================================ -->
const hostname = window.location.hostname || "127.0.0.1";
const CALDAV_URL = window.location.origin + '/calendar/';
const CONFIG_URL = window.location.origin + '/config/';
let zoom = 1.0;
let viewMode = 3; 
let currentDate = new Date();
let selectedEventId = null;
let events = [];
let contacts = [];

let isDragging = false, isResizing = false;
let isMonthDragging = false, monthDragEv = null;
window.isEditing = false;
let activeCard = null, activeEv = null;
let startMouseY = 0, startMouseX = 0, originalStartMin = 0, originalDuration = 0;
let hasMoved = false;
let originalDateStr = "";

let currentUser = "";
let canEditAll = false;
let visiblePeopleIndices = [];

let authHeader = sessionStorage.getItem('caldav_auth') || "";
function getAuthHeader() { return authHeader; }

// Global Delete Key Binding
window.addEventListener('keydown', (e) => {
    if (e.key === 'Delete' && selectedEventId && !window.isEditing && document.activeElement.tagName !== 'INPUT' && document.activeElement.tagName !== 'SELECT') {
        document.getElementById('btnDelete')?.click();
    }
});

document.getElementById('btnLogin')?.addEventListener('click', () => {
    let u = document.getElementById('loginUser').value;
    let p = document.getElementById('loginPass').value;
    authHeader = 'Basic ' + btoa(u + ':' + p);
    sessionStorage.setItem('caldav_auth', authHeader);
    document.getElementById('loginModal').style.display = 'none';
    downloadUpdates();
});

document.getElementById('btnLogout')?.addEventListener('click', () => {
    sessionStorage.removeItem('caldav_auth');
    authHeader = "";
    events = []; 
    contacts = [];
    currentUser = "";
    render(); 
    document.getElementById('loginModal').style.display = 'block';
});

function getQuarter(dateStr) {
    let parts = dateStr.split('/');
    if (parts.length === 3) {
        let y = parseInt(parts[0], 10);
        let m = parseInt(parts[1], 10) - 1; 
        return `${y}-Q${Math.floor(m / 3) + 1}`;
    }
    return '1970-Q1'; 
}

let people = ["Jack", "Bob", "Charlie", "David", "Eve", "Frank", "Grace"];
for (let i = 7; i < 255; i++) people.push("Person " + (i + 1));
const colors = ["#039BE5", "#33B679", "#8E24AA", "#F4511E", "#E67C73"];

const scrollArea = document.getElementById('scroll-area');
const headerRow = document.getElementById('header-row');
const timelineLabels = document.getElementById('timeline-labels');
const gridBg = document.getElementById('grid-bg');
const eventsLayer = document.getElementById('events-layer');
const lblDateTitle = document.getElementById('lblDateTitle');

function updateLayout() {
    const tb = document.getElementById('toolbar');
    if (tb) {
        tb.style.display = tb.dataset.open === 'true' ? 'flex' : 'none';
    }
}

document.getElementById('btnMenu')?.addEventListener('click', () => {
    const tb = document.getElementById('toolbar');
    if (tb) {
        tb.dataset.open = tb.dataset.open === 'true' ? 'false' : 'true';
        tb.style.display = tb.dataset.open === 'true' ? 'flex' : 'none';
    }
});

function updateDateTitle(text) {
    if (lblDateTitle) lblDateTitle.innerText = text;
    const mobileLbl = document.getElementById('lblDateTitleMobile');
    if (mobileLbl) mobileLbl.innerText = text;
}

function getClientY(e) { return e.touches && e.touches.length > 0 ? e.touches[0].clientY : e.clientY; }
function getClientX(e) { return e.touches && e.touches.length > 0 ? e.touches[0].clientX : e.clientX; }
function formatDate(d) { return `${d.getFullYear()}/${String(d.getMonth()+1).padStart(2, '0')}/${String(d.getDate()).padStart(2, '0')}`; }
function minToTimeStr(min) {
    let h = Math.floor(min / 60), m = min % 60;
    return `${h > 12 ? h - 12 : (h === 0 ? 12 : h)}:${String(m).padStart(2, '0')} ${h >= 12 ? 'PM' : 'AM'}`;
}

function getColCount() {
    if (viewMode === 1) return 1;
    if (viewMode === 2) return 4;
    if (viewMode === 3) return 7;
    if (viewMode === 4) {
        let offset = window.innerWidth <= 768 ? 40 : 65;
        let availableWidth = (scrollArea ? scrollArea.clientWidth : window.innerWidth) - offset;
        return Math.max(1, Math.min(255, Math.floor(availableWidth / 120)));
    }
    return 7;
}
function isPeopleView() { return viewMode === 4; }
function canEdit(ev) { return canEditAll || ev.personIdx === people.indexOf(currentUser); }

function unrollEvents() {
    let unrolled = [];
    let winStartMs = new Date(currentDate.getFullYear(), currentDate.getMonth() - 2, 1).getTime();
    let winEndMs = new Date(currentDate.getFullYear(), currentDate.getMonth() + 2, 0).getTime();
    if (viewMode === 7) winEndMs = new Date(currentDate.getFullYear() + 1, currentDate.getMonth(), 1).getTime();

    events.forEach(ev => {
        if (!ev.rrule) {
            unrolled.push({ ...ev, realId: ev.id });
            return;
        }

        let rules = {};
        ev.rrule.split(';').forEach(part => {
            let [key, val] = part.split('=');
            if (key && val) rules[key.toUpperCase()] = val;
        });

        let freq = rules['FREQ'];
        let interval = parseInt(rules['INTERVAL'] || '1', 10);
        let count = rules['COUNT'] ? parseInt(rules['COUNT'], 10) : Infinity;
        
        let untilMs = Infinity;
        if (rules['UNTIL']) {
            let u = rules['UNTIL'];
            let uYear = parseInt(u.substring(0,4), 10);
            let uMon = parseInt(u.substring(4,6), 10) - 1;
            let uDay = parseInt(u.substring(6,8), 10);
            untilMs = new Date(uYear, uMon, uDay, 23, 59, 59).getTime();
        }

        let [y, m, d] = ev.date.split('/').map(Number);
        let currentInst = new Date(y, m - 1, d);
        let instanceCount = 0;

        while (currentInst.getTime() <= winEndMs && currentInst.getTime() <= untilMs && instanceCount < count) {
            if (currentInst.getTime() >= winStartMs) {
                unrolled.push({
                    ...ev,
                    id: ev.id + "_r" + instanceCount,
                    realId: ev.id,
                    date: formatDate(currentInst)
                });
            }
            
            if (freq === 'DAILY') {
                currentInst.setDate(currentInst.getDate() + interval);
            } else if (freq === 'WEEKLY') {
                currentInst.setDate(currentInst.getDate() + (7 * interval));
            } else if (freq === 'MONTHLY') {
                currentInst.setMonth(currentInst.getMonth() + interval);
            } else if (freq === 'YEARLY') {
                currentInst.setFullYear(currentInst.getFullYear() + interval);
            } else { break; }
            
            instanceCount++;
            if (instanceCount > 730) break;
        }
    });
    return unrolled;
}

document.getElementById('btnImportRemote')?.addEventListener('click', () => {
    const modal = document.getElementById('remoteModal');
    if (modal) modal.style.display = 'block';
});

document.getElementById('btnDoImport')?.addEventListener('click', async () => {
    const url = document.getElementById('remUrl').value;
    const port = document.getElementById('remPort').value;
    const user = document.getElementById('remUser').value;
    const pass = document.getElementById('remPass').value;
    
    try {
        const auth = btoa(`${user}:${pass}`);
        const res = await fetch(`${url}:${port}/calendar/all?t=${Date.now()}`, { headers: { 'Authorization': `Basic ${auth}` } });
        if (res.ok) {
            const importedEvents = await res.json();
            events = events.concat(importedEvents.flat()); 
            
            await fetch(`${CONFIG_URL}remote`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({url, port, user, pass})
            });
            alert("Successfully connected, imported data, and saved to INI.");
            document.getElementById('remoteModal').style.display = 'none';
            render();
        } else { alert("Failed to connect: " + res.statusText); }
    } catch (e) {
        alert("Connection error: " + e.message);
    }
});

setInterval(downloadUpdates, 60000);

async function downloadUpdates() {
    if (isDragging || isResizing || isMonthDragging || window.isEditing) return; 
    
    if (!getAuthHeader()) {
        const modal = document.getElementById('loginModal');
        if (modal) modal.style.display = 'block';
        return;
    }

    try {
        let headers = { 'Authorization': getAuthHeader() };
        
        const meRes = await fetch(`${window.location.origin}/api/me?t=${Date.now()}`, { headers, cache: 'no-store' });
        if (meRes.status === 401) {
            sessionStorage.removeItem('caldav_auth');
            authHeader = "";
            document.getElementById('loginModal').style.display = 'block';
            return;
        }
        if (meRes.ok) {
            let meData = await meRes.json();
            currentUser = meData.user;
            canEditAll = meData.edit_all;
            if (meData.visible_people && meData.visible_people !== "") {
                visiblePeopleIndices = meData.visible_people.split(',').filter(x => x !== "").map(Number);
            }
        }

        const pRes = await fetch(`${CALDAV_URL}people.json?t=${Date.now()}`, { headers, cache: 'no-store' });
        if (pRes.ok) {
            try {
                let serverPeople = await pRes.json();
                for(let i = 0; i < serverPeople.length; i++) people[i] = serverPeople[i];
            } catch (e) {}
        }

        const cRes = await fetch(`${window.location.origin}/api/contacts?t=${Date.now()}`, { headers, cache: 'no-store' });
        if (cRes.ok) {
            try { contacts = await cRes.json(); } catch(e) { contacts = []; }
        }
        
        let myIdx = people.indexOf(currentUser);
        if (myIdx === -1 && currentUser && currentUser !== "anonymous") {
            myIdx = people.length;
            people.push(currentUser);
            await putPeopleToCalDAV();
        }

        const response = await fetch(`${CALDAV_URL}all?t=${Date.now()}`, { headers, cache: 'no-store' });
        if (response.ok) {
            try {
                let raw = await response.json();
                events = Array.isArray(raw) ? raw.flat() : [];
            } catch (err) {
                events = [];
            }
        }
        
        let dirtyFilter = false;
        let eventPeople = new Set();
        events.forEach(e => eventPeople.add(e.personIdx));
        
        if (visiblePeopleIndices.length === 0) {
            eventPeople.forEach(idx => {
                if (!visiblePeopleIndices.includes(idx)) visiblePeopleIndices.push(idx);
            });
            dirtyFilter = true;
        }
        if (myIdx !== -1 && !visiblePeopleIndices.includes(myIdx)) {
            visiblePeopleIndices.push(myIdx);
            dirtyFilter = true;
        }
        
        if (dirtyFilter) saveVisiblePeople();
        render(); 
    } catch (e) { 
        console.error("Sync Error", e); 
        render();
    }
}

async function saveVisiblePeople() {
    try {
        await fetch(`${window.location.origin}/api/prefs`, {
            method: 'PUT',
            headers: { 'Authorization': getAuthHeader() },
            body: visiblePeopleIndices.join(',')
        });
    } catch(e) {}
}

async function putPeopleToCalDAV() {
    try {
        await fetch(`${CALDAV_URL}people.json`, { method: 'PUT', headers: { 'Content-Type': 'application/json', 'Authorization': getAuthHeader() }, body: JSON.stringify(people) });
    } catch (e) {}
}

async function putEventToCalDAV(ev) {
    let q = getQuarter(ev.date);
    let qEvents = events.filter(e => getQuarter(e.date) === q);
    try {
        await fetch(`${CALDAV_URL}${q}.json`, { method: 'PUT', headers: { 'Content-Type': 'application/json', 'Authorization': getAuthHeader() }, body: JSON.stringify(qEvents) });
    } catch (e) {}
}

async function deleteEventFromCalDAV(evToDel) {
    let q = getQuarter(evToDel.date);
    let qEvents = events.filter(e => getQuarter(e.date) === q);
    try {
        if (qEvents.length === 0) {
            await fetch(`${CALDAV_URL}${q}.json`, { method: 'DELETE', headers: { 'Authorization': getAuthHeader() } });
        } else {
            await fetch(`${CALDAV_URL}${q}.json`, { method: 'PUT', headers: { 'Content-Type': 'application/json', 'Authorization': getAuthHeader() }, body: JSON.stringify(qEvents) });
        }
    } catch (e) {}
}

function handleVCardUpload(e) {
    let file = e.target.files[0];
    if (!file) return;
    let reader = new FileReader();
    reader.onload = function(evt) {
        let content = evt.target.result;
        let lines = content.split(/\r?\n/);
        let currentContact = null;

        for (let line of lines) {
            if (line.trim() === "BEGIN:VCARD") {
                currentContact = { username: "", fn: "", n: "", tel: "", email: "", adr: "", org: "", title: "", url: "", bday: "", note: "" };
            } else if (line.trim() === "END:VCARD") {
                if (currentContact) contacts.push(currentContact);
                currentContact = null;
            } else if (currentContact) {
                let match = line.match(/^([^:]+):(.*)$/);
                if (match) {
                    let key = match[1].split(';')[0].toUpperCase();
                    let val = match[2];
                    if (key === 'FN') currentContact.fn = val;
                    else if (key === 'N') currentContact.n = val;
                    else if (key === 'TEL') currentContact.tel = val;
                    else if (key === 'EMAIL') currentContact.email = val;
                    else if (key === 'ADR') currentContact.adr = val;
                    else if (key === 'ORG') currentContact.org = val;
                    else if (key === 'TITLE') currentContact.title = val;
                    else if (key === 'URL') currentContact.url = val;
                    else if (key === 'BDAY') currentContact.bday = val;
                    else if (key === 'NOTE') currentContact.note = val;
                }
            }
        }
        renderContactsList();
        document.getElementById('btnSaveContacts').click(); // Auto-save to server on import
    };
    reader.readAsText(file);
    e.target.value = ''; 
}

// Remove the old btnDownloadVCard event listener block, and add this:

document.getElementById('btnDownloadSingleVCard')?.addEventListener('click', () => {
    let idx = parseInt(document.getElementById('editContactIdx').value);
    if (isNaN(idx) || idx < 0 || idx >= contacts.length) return;
    
    let c = contacts[idx];
    let vcf = "BEGIN:VCARD\r\nVERSION:3.0\r\n";
    
    // Pull from the active inputs so the user can download unsaved edits
    let fn = document.getElementById('editContactFn').value || c.fullname || 'Unnamed Contact';
    let n = document.getElementById('editContactN').value;
    let email = document.getElementById('editContactEmail').value;
    let tel = document.getElementById('editContactTel').value;
    let adr = document.getElementById('editContactAdr').value;
    let org = document.getElementById('editContactOrg').value;
    let title = document.getElementById('editContactTitle').value;
    let url = document.getElementById('editContactUrl').value;
    let bday = document.getElementById('editContactBday').value;
    let note = document.getElementById('editContactNote').value;

    vcf += `FN:${fn}\r\n`;
    if (n) vcf += `N:${n}\r\n`;
    if (email) vcf += `EMAIL:${email}\r\n`;
    if (tel) vcf += `TEL:${tel}\r\n`;
    if (adr) vcf += `ADR:${adr}\r\n`;
    if (org) vcf += `ORG:${org}\r\n`;
    if (title) vcf += `TITLE:${title}\r\n`;
    if (url) vcf += `URL:${url}\r\n`;
    if (bday) vcf += `BDAY:${bday}\r\n`;
    if (note) vcf += `NOTE:${note}\r\n`;
    vcf += "END:VCARD\r\n";
    
    let blob = new Blob([vcf], {type: "text/vcard"});
    let urlObj = URL.createObjectURL(blob);
    let a = document.createElement('a');
    a.href = urlObj;
    let safeName = fn.replace(/[^a-z0-9]/gi, '_').toLowerCase();
    a.download = safeName + ".vcf";
    a.click();
    URL.revokeObjectURL(urlObj);
});

window.openContactEditor = function(idx) {
    let c = contacts[idx];
    document.getElementById('editContactIdx').value = idx;
    document.getElementById('editContactIdx').dataset.oldUsername = c.username || '';
    
    let dl = document.getElementById('availableUsernames');
    dl.innerHTML = '';
    
    // Filter available usernames (don't show ones already assigned to *other* contacts)
    people.forEach(p => {
        if (p && !p.startsWith("Person ")) {
            let isUsed = contacts.some((otherC, otherIdx) => otherIdx !== idx && otherC.username === p);
            if (!isUsed) {
                dl.innerHTML += `<option value="${p}">`;
            }
        }
    });
    
    document.getElementById('editContactUsername').value = c.username || '';
    document.getElementById('editContactFn').value = c.fn || c.fullname || '';
    document.getElementById('editContactN').value = c.n || '';
    document.getElementById('editContactEmail').value = c.email || '';
    document.getElementById('editContactTel').value = c.tel || '';
    document.getElementById('editContactAdr').value = c.adr || '';
    document.getElementById('editContactOrg').value = c.org || '';
    document.getElementById('editContactTitle').value = c.title || '';
    document.getElementById('editContactUrl').value = c.url || '';
    document.getElementById('editContactBday').value = c.bday || '';
    document.getElementById('editContactNote').value = c.note || '';
    
    document.getElementById('contactEditorModal').style.display = 'flex';
};

document.getElementById('btnSaveSingleContact')?.addEventListener('click', async () => {
    let idx = parseInt(document.getElementById('editContactIdx').value);
    if (isNaN(idx) || idx < 0 || idx >= contacts.length) return;
    
    let c = contacts[idx];
    let newUsername = document.getElementById('editContactUsername').value.trim();
    let oldUsername = document.getElementById('editContactIdx').dataset.oldUsername || '';
    
    if (newUsername !== "") {
        let isUsed = contacts.some((otherC, otherIdx) => otherIdx !== idx && otherC.username === newUsername);
        if (isUsed) {
            alert("This username is already linked to another contact. Unique links are required.");
            return;
        }
    }
    
    c.username = newUsername;
    c.fn = document.getElementById('editContactFn').value;
    c.n = document.getElementById('editContactN').value;
    c.email = document.getElementById('editContactEmail').value;
    c.tel = document.getElementById('editContactTel').value;
    c.adr = document.getElementById('editContactAdr').value;
    c.org = document.getElementById('editContactOrg').value;
    c.title = document.getElementById('editContactTitle').value;
    c.url = document.getElementById('editContactUrl').value;
    c.bday = document.getElementById('editContactBday').value;
    c.note = document.getElementById('editContactNote').value;
    
    delete c.fullname; // Cleanup legacy property if present
    
    if (newUsername !== oldUsername && newUsername !== "") {
        let payload = oldUsername ? `${oldUsername}|${newUsername}` : newUsername;
        try {
            await fetch(`${window.location.origin}/api/user`, {
                method: 'PUT',
                headers: { 'Authorization': getAuthHeader() },
                body: payload
            });
            let pIdx = people.indexOf(oldUsername);
            if (oldUsername && pIdx !== -1) {
                people[pIdx] = newUsername;
            } else if (people.indexOf(newUsername) === -1) {
                people.push(newUsername);
            }
            putPeopleToCalDAV();
        } catch(e) {}
    }
    
    document.getElementById('contactEditorModal').style.display = 'none';
    renderContactsList();
});

function renderContactsList() {
    const list = document.getElementById('contactsList');
    if (!list) return;
    list.innerHTML = '';
    const filterText = (document.getElementById('contactFilter')?.value || '').toLowerCase();
    
    contacts.forEach((c, idx) => {
        let displayFn = c.fn || c.fullname || '';
        if (filterText && !(c.username || '').toLowerCase().includes(filterText) && !displayFn.toLowerCase().includes(filterText)) return;
        
        let div = document.createElement('div');
        div.style.display = 'flex';
        div.style.justifyContent = 'space-between';
        div.style.alignItems = 'center';
        div.style.padding = '8px 12px';
        div.style.borderBottom = '1px solid #dadce0';
        
        let linkText = c.username ? ` (Linked: ${c.username})` : '';
        div.innerHTML = `
            <div style="display:flex; flex-direction:column;">
                <b style="font-size:14px; color:#202124;">${displayFn || 'Unnamed Contact'}</b>
                <span style="font-size:11px; color:#5f6368;">${c.email || 'No email'}${linkText}</span>
            </div>
            <div style="display:flex; gap: 6px;">
                <button onclick="openContactEditor(${idx})" style="cursor:pointer; border:1px solid #ccc; border-radius:4px; padding:4px 12px; background:#f8f9fa;">Edit</button>
                <button onclick="contacts.splice(${idx}, 1); renderContactsList();" style="cursor:pointer; color:#d93025; border:1px solid #dadce0; border-radius:4px; padding:4px 12px; background:#fff;">Delete</button>
            </div>
        `;
        list.appendChild(div);
    });
}

document.getElementById('btnContacts')?.addEventListener('click', () => {
    renderContactsList();
    document.getElementById('contactsModal').style.display = 'flex';
});

document.getElementById('btnAddContact')?.addEventListener('click', () => {
    contacts.push({ username: "", fn: "", n: "", tel: "", email: "", adr: "", org: "", title: "", url: "", bday: "", note: "" });
    renderContactsList();
    openContactEditor(contacts.length - 1);
});

document.getElementById('btnSaveContacts')?.addEventListener('click', async () => {
    await fetch(`${window.location.origin}/api/contacts`, {
        method: 'PUT',
        headers: { 'Authorization': getAuthHeader(), 'Content-Type': 'application/json' },
        body: JSON.stringify(contacts)
    });
    document.getElementById('contactsModal').style.display = 'none';
    render();
});

function renderTimeline() {
    if (!gridBg || !timelineLabels || !headerRow) return;
    
    let myIdx = people.indexOf(currentUser);
    if (myIdx !== -1 && !visiblePeopleIndices.includes(myIdx)) {
        visiblePeopleIndices.push(myIdx);
    }

    let cols = getColCount();
    let activeCols = isPeopleView() ? visiblePeopleIndices.length : cols;
    if (activeCols === 0) activeCols = 1;
    
    updateDateTitle(currentDate.toLocaleDateString(undefined, { month: 'long', day: 'numeric', year: 'numeric' }));
    
    headerRow.innerHTML = '';
    for(let i=0; i<activeCols; i++) {
        let div = document.createElement('div');
        div.className = 'col-header';
        
        if (isPeopleView()) {
            let pIdx = visiblePeopleIndices[i];
            let shortName = people[pIdx] || `Person ${pIdx + 1}`;
            let contactMatch = contacts.find(c => c.username === shortName);
            div.innerText = (contactMatch && (contactMatch.fn || contactMatch.fullname)) ? (contactMatch.fn || contactMatch.fullname) : shortName;
            
            div.ondblclick = () => {
                let input = document.createElement('input');
                input.className = 'edit-person-input';
                input.value = people[pIdx] || "";
                div.innerHTML = '';
                div.appendChild(input);
                input.focus();
                
                input.onblur = input.onkeydown = async (ev) => {
                    if (ev.type === 'keydown' && ev.key !== 'Enter') return;
                    input.onblur = null; input.onkeydown = null;
                    
                    let oldName = people[pIdx] || "";
                    let newName = input.value.trim() || `Person ${pIdx + 1}`;
                    while (people.length <= pIdx) {
                        people.push(`Person ${people.length + 1}`);
                    }
                    people[pIdx] = newName;
                    putPeopleToCalDAV();
                    
                    if (newName !== oldName) {
                        try {
                            let payload = oldName ? `${oldName}|${newName}` : newName;
                            await fetch(`${window.location.origin}/api/user`, {
                                method: 'PUT',
                                headers: { 'Authorization': getAuthHeader() },
                                body: payload
                            });
                        } catch(e) {}
                    }
                    
                    render();
                };
            };
        } else {
            let d = new Date(currentDate);
            d.setDate(d.getDate() + i);
            div.innerText = d.toLocaleDateString(undefined, { weekday: 'short', month: 'numeric', day: 'numeric' });
        }
        headerRow.appendChild(div);
    }

    const totalHeight = 1440 * zoom;
    timelineLabels.style.height = `${totalHeight}px`;
    gridBg.style.height = `${totalHeight}px`;

    timelineLabels.innerHTML = '';
    for(let i=0; i<=24; i++) {
        let lbl = document.createElement('div');
        lbl.className = 'time-label';
        lbl.style.top = `${i * 60 * zoom}px`;
        if(i > 0 && i < 24) lbl.innerText = minToTimeStr(i * 60);
        timelineLabels.appendChild(lbl);
    }

    gridBg.innerHTML = ''; 
    gridBg.style.background = '';
    gridBg.style.backgroundImage = 'linear-gradient(#e0e0e0 1px, transparent 1px), linear-gradient(90deg, #dadce0 1px, transparent 1px)';
    gridBg.style.backgroundSize = `calc(100% / ${activeCols}) ${60 * zoom}px`;
    
    if (eventsLayer) eventsLayer.innerHTML = '';

    let displayEvents = typeof unrollEvents === 'function' ? unrollEvents() : events;

    displayEvents.forEach(ev => {
        if (!isPeopleView() && viewMode <= 3) {
            if (myIdx !== -1 && ev.personIdx !== myIdx && visiblePeopleIndices.length > 0 && !visiblePeopleIndices.includes(ev.personIdx)) {
                return;
            }
        } else if (isPeopleView()) {
            if (visiblePeopleIndices.length > 0 && !visiblePeopleIndices.includes(ev.personIdx)) {
                return;
            }
        }

        let colIdx = -1;
        if (isPeopleView()) {
            if (ev.date === formatDate(currentDate)) {
                let dispIdx = visiblePeopleIndices.indexOf(ev.personIdx);
                if (dispIdx !== -1) colIdx = dispIdx;
            }
        } else {
            let evDate = new Date(ev.date.replace(/\//g, '-') + 'T12:00:00');
            let diffDays = Math.round((evDate.getTime() - new Date(currentDate.getFullYear(), currentDate.getMonth(), currentDate.getDate(), 12, 0, 0).getTime()) / 86400000);
            if (diffDays >= 0 && diffDays < cols) colIdx = diffDays;
        }

        if (colIdx >= 0 && colIdx < activeCols && eventsLayer) {
            let card = document.createElement('div');
            card.className = 'event-card' + (selectedEventId === ev.id ? ' selected' : '');
            card.style.top = `${ev.startMin * zoom}px`;
            card.style.height = `${ev.duration * zoom}px`;
            card.style.left = `${(colIdx / activeCols) * 100}%`;
            card.style.width = `calc(${100 / activeCols}% - 6px)`;
            card.style.marginLeft = '3px';
            card.style.backgroundColor = ev.color;
            card.dataset.id = ev.id;
            
            card.innerHTML = `
                <div class="event-title">${ev.title}</div>
                <div class="event-time">${minToTimeStr(ev.startMin)} - ${minToTimeStr(ev.startMin + ev.duration)}</div>
                <div class="resize-handle"></div>
            `;
            
            card.ondblclick = (e) => {
                e.stopPropagation(); 
                if (!canEdit(ev)) { alert("You do not have permission to edit this event."); return; }
                
                window.isEditing = true;
                isDragging = false;
                
                let titleDiv = card.querySelector('.event-title') || card;
                let currentTitle = ev.title;
                
                let input = document.createElement('input');
                input.type = 'text';
                input.value = currentTitle;
                input.style.width = '100%';
                input.style.color = '#000';
                input.style.pointerEvents = 'auto'; 
                input.onmousedown = (e) => e.stopPropagation();
                input.ontouchstart = (e) => e.stopPropagation();
                
                titleDiv.innerHTML = '';
                titleDiv.appendChild(input);
                input.focus();
                
                input.onblur = input.onkeydown = (keyEv) => {
                    if (keyEv.type === 'keydown' && keyEv.key !== 'Enter') return;
                    input.onblur = null; input.onkeydown = null;
                    let realId = ev.id.includes('_r') ? ev.id.split('_r')[0] : ev.id;
                    let targetEv = events.find(x => x.id === realId);
                    if (targetEv) {
                        targetEv.title = input.value || "Untitled Event";
                        putEventToCalDAV(targetEv);
                    }
                    window.isEditing = false;
                    render();
                };
            };

            card.addEventListener('mousedown', handleDrag);
            card.addEventListener('touchstart', handleDrag, {passive: false});
            card.querySelector('.resize-handle').addEventListener('mousedown', handleResize);
            card.querySelector('.resize-handle').addEventListener('touchstart', handleResize, {passive: false});
            eventsLayer.appendChild(card);
        }
    });
}

function renderMonth() {
    if (!gridBg || !headerRow || !timelineLabels) return;
    
    updateDateTitle(currentDate.toLocaleDateString(undefined, { month: 'long', year: 'numeric' }));
    headerRow.innerHTML = '';
    const days = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
    days.forEach(d => {
        let div = document.createElement('div');
        div.className = 'col-header';
        div.style.flex = '1';
        div.style.textAlign = 'center';
        div.innerText = d;
        headerRow.appendChild(div);
    });

    timelineLabels.innerHTML = '';
    gridBg.style.background = 'none';
    gridBg.innerHTML = '';
    if (eventsLayer) eventsLayer.innerHTML = '';
    
    let grid = document.createElement('div');
    grid.className = 'month-grid';
    
    let y = currentDate.getFullYear();
    let m = currentDate.getMonth();
    let firstDay = new Date(y, m, 1).getDay();
    let daysInMonth = new Date(y, m + 1, 0).getDate();
    
    let cellCount = 0;
    let displayEvents = typeof unrollEvents === 'function' ? unrollEvents() : events;
    let myIdx = people.indexOf(currentUser);

    for (let row = 0; row < 6; row++) {
        for (let col = 0; col < 7; col++) {
            let cell = document.createElement('div');
            cell.className = 'month-cell';
            
            let dayNum = cellCount - firstDay + 1;
            let cellDateStr;
            let displayNum;
            let isOtherMonth = false;

            if (dayNum <= 0) {
                let pD = new Date(y, m, 0); 
                displayNum = pD.getDate() + dayNum;
                cellDateStr = formatDate(new Date(y, m - 1, displayNum));
                isOtherMonth = true;
            } else if (dayNum > daysInMonth) {
                displayNum = dayNum - daysInMonth;
                cellDateStr = formatDate(new Date(y, m + 1, displayNum));
                isOtherMonth = true;
            } else {
                displayNum = dayNum;
                cellDateStr = `${y}/${String(m+1).padStart(2,'0')}/${String(dayNum).padStart(2,'0')}`;
            }

            cell.innerHTML = `<div class="month-day-num" style="${isOtherMonth ? 'color: #9aa0a6;' : ''}">${displayNum}</div>`;
            cell.dataset.date = cellDateStr;
            cell.dataset.other = isOtherMonth ? 'true' : 'false';
            
            let validEvents = displayEvents.filter(e => {
                if (e.date !== cellDateStr) return false;
                if (myIdx !== -1 && e.personIdx === myIdx) return true;
                if (visiblePeopleIndices.length > 0 && !visiblePeopleIndices.includes(e.personIdx)) return false;
                return true;
            });
            
            validEvents.forEach(ev => {
                let evDiv = document.createElement('div');
                evDiv.className = 'month-event';
                evDiv.style.backgroundColor = ev.color;
                evDiv.innerText = ev.title;
                evDiv.dataset.id = ev.id;
                if(selectedEventId === ev.id) evDiv.style.outline = "2px solid #000";
                
                evDiv.onclick = (e) => { 
                    e.stopPropagation(); 
                    selectedEventId = ev.id; 
                    document.querySelectorAll('.month-event').forEach(el => el.style.outline = 'none');
                    evDiv.style.outline = "2px solid #000"; 
                };
                
                evDiv.addEventListener('mousedown', startMonthDrag);
                evDiv.addEventListener('touchstart', startMonthDrag, {passive: false});
                
                evDiv.ondblclick = (e) => {
                    e.stopPropagation();
                    if (!canEdit(ev)) { alert("You do not have permission to edit this event."); return; }
                    
                    window.isEditing = true;
                    
                    let input = document.createElement('input');
                    input.type = 'text';
                    input.value = ev.title;
                    input.style.width = '100%';
                    input.style.color = '#000';
                    input.style.pointerEvents = 'auto'; 
                    input.onmousedown = (e) => e.stopPropagation();
                    input.ontouchstart = (e) => e.stopPropagation();
                    
                    evDiv.innerHTML = '';
                    evDiv.appendChild(input);
                    input.focus();
                    
                    input.onblur = input.onkeydown = (keyEv) => {
                        if (keyEv.type === 'keydown' && keyEv.key !== 'Enter') return;
                        input.onblur = null; input.onkeydown = null;
                        
                        let realId = ev.id.includes('_r') ? ev.id.split('_r')[0] : ev.id;
                        let targetEv = events.find(x => x.id === realId);
                        if (targetEv) {
                            targetEv.title = input.value || "Untitled Event";
                            putEventToCalDAV(targetEv);
                        }
                        window.isEditing = false;
                        render();
                    };
                };
                cell.appendChild(evDiv);
            });
            
            cell.ondblclick = () => {
                let targetIdx = people.indexOf(currentUser);
                if (targetIdx === -1 && currentUser) {
                    targetIdx = people.length;
                    people.push(currentUser);
                    putPeopleToCalDAV();
                } else if (targetIdx === -1) {
                    targetIdx = 0; 
                }
                
                let newEv = { 
                    id: Date.now().toString(), 
                    title: "New Event", 
                    startMin: 540, 
                    duration: 60, 
                    color: colors[events.length % colors.length], 
                    date: cellDateStr, 
                    personIdx: targetIdx 
                };
                events.push(newEv);
                putEventToCalDAV(newEv);
                render();
            };
            
            if (isOtherMonth) {
                cell.style.backgroundColor = '#f1f3f4';
            }
            grid.appendChild(cell);
            cellCount++;
        }
    }
    gridBg.appendChild(grid);
}

function startMonthDrag(e) {
    e.stopPropagation();
    if (window.isEditing) return;
    selectedEventId = this.dataset.id;
    let realId = selectedEventId.includes('_r') ? selectedEventId.split('_r')[0] : selectedEventId;
    monthDragEv = events.find(x => x.id === realId);
    
    if (!monthDragEv || !canEdit(monthDragEv)) {
        monthDragEv = null;
        return;
    }
    
    isMonthDragging = true;
    hasMoved = false;
    document.querySelectorAll('.month-event').forEach(el => el.style.outline = 'none');
    this.style.outline = "2px solid #000";
}

function renderUpcoming() {
    if (!gridBg || !headerRow || !timelineLabels) return;
    updateDateTitle("Upcoming Schedule");
    headerRow.innerHTML = '';
    timelineLabels.innerHTML = '';
    gridBg.style.background = 'none';
    gridBg.innerHTML = '';
    if (eventsLayer) eventsLayer.innerHTML = '';

    let container = document.createElement('div');
    container.className = 'upcoming-list';
    
    let displayEvents = typeof unrollEvents === 'function' ? unrollEvents() : events;
    let myIdx = people.indexOf(currentUser);
    
    let validEvents = displayEvents.filter(e => {
        if (e.date < formatDate(new Date())) return false;
        if (myIdx !== -1 && e.personIdx === myIdx) return true;
        if (visiblePeopleIndices.length > 0 && !visiblePeopleIndices.includes(e.personIdx)) return false;
        return true;
    });
    
    let sorted = validEvents.sort((a,b) => a.date.localeCompare(b.date) || a.startMin - b.startMin);
    
    sorted.forEach(ev => {
        let card = document.createElement('div');
        card.className = 'upcoming-card';
        let shortName = people[ev.personIdx] || "";
        let contactMatch = contacts.find(c => c.username === shortName);
        let p = (contactMatch && (contactMatch.fn || contactMatch.fullname)) ? (contactMatch.fn || contactMatch.fullname) : shortName;
        card.innerHTML = `
            <div class="upcoming-color" style="background:${ev.color}"></div>
            <div class="upcoming-details">
                <h3>${ev.title}</h3>
                <p>${ev.date} &nbsp;|&nbsp; ${minToTimeStr(ev.startMin)} - ${minToTimeStr(ev.startMin + ev.duration)} &nbsp;|&nbsp; ${p}</p>
            </div>
        `;
        container.appendChild(card);
    });
    
    gridBg.appendChild(container);
}

function render() {
    if (viewMode === 6) renderMonth();
    else if (viewMode === 7) renderUpcoming();
    else renderTimeline();
}

window.addEventListener('resize', () => {
    // Prevent layout thrashing when the mobile keyboard changes the viewport height
    if (window.isEditing) return; 
    
    render();
    updateLayout();
});

function handleDrag(e) {
    if (e.target.classList.contains('resize-handle') || e.target.tagName === 'INPUT') return;
    if (window.isEditing) return;
    
    selectedEventId = e.currentTarget.dataset.id;
    let realId = selectedEventId.includes('_r') ? selectedEventId.split('_r')[0] : selectedEventId;
    activeEv = events.find(ev => ev.id === realId);
    if (!activeEv) return;
    
    if (!canEdit(activeEv)) {
        activeEv = null;
        return;
    }
    
    originalDateStr = activeEv.date;
    
    if (e.ctrlKey) {
        let clonedEv = { ...activeEv, id: Date.now().toString() };
        events.push(clonedEv);
        activeEv = clonedEv;
        selectedEventId = clonedEv.id;
        render(); 
    } else {
        document.querySelectorAll('.event-card').forEach(c => c.classList.remove('selected'));
        e.currentTarget.classList.add('selected');
    }

    originalStartMin = activeEv.startMin;
    startMouseY = getClientY(e);
    startMouseX = getClientX(e);
    isDragging = true;
    hasMoved = false;
    
    activeCard = document.querySelector(`.event-card[data-id='${selectedEventId}']`);
}

function handleResize(e) {
    e.stopPropagation();
    if (window.isEditing) return;
    activeCard = e.target.parentElement;
    selectedEventId = activeCard.dataset.id;
    let realId = selectedEventId.includes('_r') ? selectedEventId.split('_r')[0] : selectedEventId;
    activeEv = events.find(ev => ev.id === realId);
    
    if (!activeEv || !canEdit(activeEv)) {
        activeEv = null;
        return;
    }
    
    isResizing = true;
    hasMoved = true;
    startMouseY = getClientY(e);
    originalDuration = activeEv.duration;
}

function handleMove(e) {
    if (isMonthDragging && monthDragEv) {
        if (e.cancelable) e.preventDefault();
        hasMoved = true;
        let target = document.elementFromPoint(getClientX(e), getClientY(e));
        document.querySelectorAll('.month-cell').forEach(c => c.style.backgroundColor = c.dataset.other === 'true' ? '#f1f3f4' : '');
        if (target && target.closest('.month-cell')) {
            target.closest('.month-cell').style.backgroundColor = '#e8eaed';
        }
        return;
    }

    if (!activeCard || !activeEv) return;
    
    if (isDragging || isResizing) {
        if (e.cancelable) e.preventDefault();
    }
    
    if (isDragging) {
        let deltaY = getClientY(e) - startMouseY;
        let deltaX = getClientX(e) - startMouseX;
        
        if (!hasMoved && Math.abs(deltaY) < 5 && Math.abs(deltaX) < 5) return;
        hasMoved = true;

        let deltaMins = Math.round(deltaY / zoom / 15) * 15; 
        let newMin = Math.max(0, Math.min(1440 - activeEv.duration, originalStartMin + deltaMins));
        activeEv.startMin = newMin;
        activeCard.style.top = `${newMin * zoom}px`;
        activeCard.querySelector('.event-time').innerText = `${minToTimeStr(newMin)} - ${minToTimeStr(newMin + activeEv.duration)}`;

        let rect = gridBg.getBoundingClientRect();
        let cols = getColCount();
        let activeCols = isPeopleView() ? visiblePeopleIndices.length : cols;
        if (activeCols === 0) activeCols = 1;
        
        let xOffset = getClientX(e) - rect.left;
        let newColIdx = Math.max(0, Math.min(activeCols - 1, Math.floor(xOffset / (rect.width / activeCols))));
        
        if (isPeopleView()) {
            let tgtIdx = visiblePeopleIndices[newColIdx];
            if (!canEditAll && tgtIdx !== people.indexOf(currentUser)) {
            } else {
                activeEv.personIdx = tgtIdx;
            }
        } else {
            let dt = new Date(currentDate.getFullYear(), currentDate.getMonth(), currentDate.getDate()); 
            dt.setDate(dt.getDate() + newColIdx);
            activeEv.date = formatDate(dt);
        }
        
        let displayCol = isPeopleView() ? visiblePeopleIndices.indexOf(activeEv.personIdx) : newColIdx;
        activeCard.style.left = `${(displayCol / activeCols) * 100}%`;
        
    } else if (isResizing) {
        let deltaY = getClientY(e) - startMouseY;
        let deltaMins = Math.round(deltaY / zoom / 15) * 15;
        let newDur = Math.max(15, Math.min(1440 - activeEv.startMin, originalDuration + deltaMins));
        activeEv.duration = newDur;
        activeCard.style.height = `${newDur * zoom}px`;
        activeCard.querySelector('.event-time').innerText = `${minToTimeStr(activeEv.startMin)} - ${minToTimeStr(activeEv.startMin + newDur)}`;
    }
}

function handleUp(e) {
    if (isMonthDragging && monthDragEv) {
        if (hasMoved) {
            let clientX = e.changedTouches ? e.changedTouches[0].clientX : e.clientX;
            let clientY = e.changedTouches ? e.changedTouches[0].clientY : e.clientY;
            let target = document.elementFromPoint(clientX, clientY);
            let cell = target ? target.closest('.month-cell') : null;
            if (cell && cell.dataset.date && monthDragEv.date !== cell.dataset.date) {
                let oldDateStr = monthDragEv.date;
                monthDragEv.date = cell.dataset.date;
                
                if (getQuarter(oldDateStr) !== getQuarter(monthDragEv.date)) {
                    let oldQ = getQuarter(oldDateStr);
                    let oldQEvents = events.filter(e => getQuarter(e.date) === oldQ);
                    if (oldQEvents.length === 0) {
                        fetch(`${CALDAV_URL}${oldQ}.json`, { method: 'DELETE', headers: { 'Authorization': getAuthHeader() } });
                    } else {
                        fetch(`${CALDAV_URL}${oldQ}.json`, { method: 'PUT', headers: { 'Content-Type': 'application/json', 'Authorization': getAuthHeader() }, body: JSON.stringify(oldQEvents) });
                    }
                }
                putEventToCalDAV(monthDragEv);
                render();
            }
        }
        isMonthDragging = false;
        monthDragEv = null;
        document.querySelectorAll('.month-cell').forEach(c => c.style.backgroundColor = c.dataset.other === 'true' ? '#f1f3f4' : '');
        return;
    }

    if ((isDragging || isResizing) && activeEv) {
        if (hasMoved) {
            if (originalDateStr && getQuarter(originalDateStr) !== getQuarter(activeEv.date)) {
                let oldQ = getQuarter(originalDateStr);
                let oldQEvents = events.filter(e => getQuarter(e.date) === oldQ);
                if (oldQEvents.length === 0) {
                    fetch(`${CALDAV_URL}${oldQ}.json`, { method: 'DELETE', headers: { 'Authorization': getAuthHeader() } });
                } else {
                    fetch(`${CALDAV_URL}${oldQ}.json`, { method: 'PUT', headers: { 'Content-Type': 'application/json', 'Authorization': getAuthHeader() }, body: JSON.stringify(oldQEvents) });
                }
            }
            putEventToCalDAV(activeEv);
        }
    }
    isDragging = false;
    isResizing = false;
    activeCard = null;
    activeEv = null;
}

window.addEventListener('mousemove', handleMove, {passive: false});
window.addEventListener('touchmove', handleMove, {passive: false});
window.addEventListener('mouseup', handleUp);
window.addEventListener('touchend', handleUp);

if (gridBg) {
    gridBg.addEventListener('dblclick', e => {
        if (viewMode >= 6) return; 
        let rect = gridBg.getBoundingClientRect();
        
        let y = getClientY(e) - rect.top;
        let x = getClientX(e) - rect.left;
        
        let stMin = Math.max(0, Math.min(1440 - 60, Math.floor(y / zoom / 30) * 30));
        let cols = getColCount();
        let activeCols = isPeopleView() ? visiblePeopleIndices.length : cols;
        if (activeCols === 0) activeCols = 1;
        
        let colIdx = Math.max(0, Math.min(activeCols - 1, Math.floor(x / (rect.width / activeCols))));
        
        let dt = new Date(currentDate.getFullYear(), currentDate.getMonth(), currentDate.getDate());
        if (!isPeopleView()) dt.setDate(dt.getDate() + colIdx);
        
        let pIdx = isPeopleView() ? visiblePeopleIndices[colIdx] : people.indexOf(currentUser);
        if (pIdx === -1 || pIdx === undefined) {
            pIdx = people.indexOf(currentUser);
            if (pIdx === -1 && currentUser) {
                pIdx = people.length;
                people.push(currentUser);
                putPeopleToCalDAV();
            } else if (pIdx === -1) {
                pIdx = 0;
            }
        }
        
        if (!canEditAll && pIdx !== people.indexOf(currentUser)) {
            alert("You can only create events on your own schedule.");
            return;
        }
        
        let newEv = {
            id: Date.now().toString(), title: "New Event", startMin: stMin, duration: 60,
            color: colors[events.length % colors.length], date: formatDate(dt), personIdx: pIdx
        };
        events.push(newEv);
        putEventToCalDAV(newEv);
        render();
    });
}

document.getElementById('comboView')?.addEventListener('change', e => { 
    viewMode = parseInt(e.target.value); 
    document.getElementById('btnFilterPeople').style.display = 'inline-block';
    document.getElementById('btnCloneForUsers').style.display = 'inline-block';
    render(); 
});
document.getElementById('btnPrev')?.addEventListener('click', () => { 
    if (viewMode === 6) currentDate.setMonth(currentDate.getMonth() - 1);
    else currentDate.setDate(currentDate.getDate() - (viewMode === 1 ? 1 : (viewMode === 2 ? 4 : 7))); 
    render(); 
});
document.getElementById('btnNext')?.addEventListener('click', () => { 
    if (viewMode === 6) currentDate.setMonth(currentDate.getMonth() + 1);
    else currentDate.setDate(currentDate.getDate() + (viewMode === 1 ? 1 : (viewMode === 2 ? 4 : 7))); 
    render(); 
});
document.getElementById('btnZoomIn')?.addEventListener('click', () => { zoom = Math.min(2.5, zoom + 0.2); render(); });
document.getElementById('btnZoomOut')?.addEventListener('click', () => { zoom = Math.max(0.6, zoom - 0.2); render(); });
document.getElementById('btnSync')?.addEventListener('click', downloadUpdates);

document.getElementById('btnCopy')?.addEventListener('click', () => {
    if (selectedEventId) {
        let realId = selectedEventId.includes('_r') ? selectedEventId.split('_r')[0] : selectedEventId;
        let ev = events.find(e => e.id === realId);
        if (ev && canEdit(ev)) {
            let newEv = { ...ev, id: Date.now().toString(), startMin: Math.min(1440 - ev.duration, ev.startMin + 30) };
            events.push(newEv);
            putEventToCalDAV(newEv);
            selectedEventId = newEv.id;
            render();
        } else { alert("You do not have permission to duplicate this event."); }
    }
});

document.getElementById('btnDelete')?.addEventListener('click', () => {
    if (selectedEventId) {
        let realId = selectedEventId.includes('_r') ? selectedEventId.split('_r')[0] : selectedEventId;
        let evToDel = events.find(e => e.id === realId);
        if (evToDel && !canEdit(evToDel)) { alert("You do not have permission to delete this event."); return; }
        
        events = events.filter(e => e.id !== realId);
        if (evToDel) deleteEventFromCalDAV(evToDel);
        selectedEventId = null;
        render();
    }
});

document.getElementById('btnFilterPeople')?.addEventListener('click', () => {
    const list = document.getElementById('peopleFilterList');
    list.innerHTML = '';
    people.forEach((p, idx) => {
        if (!p) return;
        let div = document.createElement('div');
        let chk = document.createElement('input');
        chk.type = 'checkbox';
        chk.value = idx;
        chk.checked = visiblePeopleIndices.includes(idx);
        chk.id = 'chk_person_' + idx;
        
        let lbl = document.createElement('label');
        lbl.htmlFor = 'chk_person_' + idx;
        lbl.innerText = p;
        
        div.appendChild(chk);
        div.appendChild(lbl);
        list.appendChild(div);
    });
    document.getElementById('peopleFilterModal').style.display = 'block';
});

document.getElementById('btnSavePeopleFilter')?.addEventListener('click', () => {
    let checks = document.querySelectorAll('#peopleFilterList input[type="checkbox"]');
    visiblePeopleIndices = [];
    checks.forEach(chk => {
        if (chk.checked) visiblePeopleIndices.push(parseInt(chk.value));
    });
    
    let myIdx = people.indexOf(currentUser);
    if (myIdx !== -1 && !visiblePeopleIndices.includes(myIdx)) {
        visiblePeopleIndices.push(myIdx);
    }
    
    saveVisiblePeople();
    document.getElementById('peopleFilterModal').style.display = 'none';
    render();
});

document.getElementById('btnCloneForUsers')?.addEventListener('click', () => {
    if (!selectedEventId) {
        alert("Please select an event first.");
        return;
    }
    let realId = selectedEventId.includes('_r') ? selectedEventId.split('_r')[0] : selectedEventId;
    let ev = events.find(e => e.id === realId);
    
    if (!ev || !canEdit(ev)) {
        alert("You do not have permission to clone this event.");
        return;
    }
    
    const list = document.getElementById('cloneUsersList');
    list.innerHTML = '';
    people.forEach((p, idx) => {
        if (!p) return;
        let div = document.createElement('div');
        let chk = document.createElement('input');
        chk.type = 'checkbox';
        chk.value = idx;
        chk.id = 'chk_clone_person_' + idx;
        
        let lbl = document.createElement('label');
        lbl.htmlFor = 'chk_clone_person_' + idx;
        lbl.innerText = p;
        
        div.appendChild(chk);
        div.appendChild(lbl);
        list.appendChild(div);
    });
    document.getElementById('cloneUsersModal').style.display = 'block';
});

document.getElementById('btnDoCloneUsers')?.addEventListener('click', () => {
    let realId = selectedEventId.includes('_r') ? selectedEventId.split('_r')[0] : selectedEventId;
    let ev = events.find(e => e.id === realId);
    
    if (!ev) return;
    
    let checks = document.querySelectorAll('#cloneUsersList input[type="checkbox"]:checked');
    if (checks.length === 0) {
        alert("Please select at least one user.");
        return;
    }
    
    checks.forEach(chk => {
        let targetPersonIdx = parseInt(chk.value);
        let newEv = {
            ...ev,
            id: Date.now().toString() + Math.floor(Math.random() * 1000),
            personIdx: targetPersonIdx
        };
        events.push(newEv);
        putEventToCalDAV(newEv);
    });
    
    document.getElementById('cloneUsersModal').style.display = 'none';
    render();
});

document.getElementById('btnPrint')?.addEventListener('click', () => {
    if (!window.jspdf) {
        alert("jsPDF library not loaded. Ensure your jspdf.umd.min.js file is downloaded and placed in the same folder.");
        return;
    }

    const { jsPDF } = window.jspdf;
    const doc = new jsPDF({ orientation: 'portrait', unit: 'pt', format: 'letter' });
    
    let w = 612, h = 792; 
    let rPage = { left: 18, top: 18, right: w - 18, bottom: h - 18 };
    
    let titleH = 45, subH = 25; 
    let gridTop = rPage.top + titleH + subH;
    let gridH = rPage.bottom - gridTop;
    
    let cols = getColCount();
    let activeCols = isPeopleView() ? visiblePeopleIndices.length : cols;
    if (activeCols === 0) activeCols = 1;
    
    let timeColW = 54;
    let dayColW = (rPage.right - rPage.left - timeColW) / activeCols;

    doc.setFont("helvetica", "bold");
    doc.setFontSize(18);
    doc.setTextColor(32, 33, 36);
    if (lblDateTitle) doc.text(lblDateTitle.innerText, rPage.left, rPage.top + 18);

    let combo = document.getElementById('comboView');
    let viewDesc = combo.options[combo.selectedIndex].text;
    doc.setFont("helvetica", "normal");
    doc.setFontSize(11);
    doc.setTextColor(112, 117, 122);
    doc.text(`Calendar Profile: ${viewDesc}`, rPage.left, rPage.top + 34);

    doc.setFillColor(248, 249, 250);
    doc.rect(rPage.left + timeColW, rPage.top + titleH, rPage.right - (rPage.left + timeColW), subH, "F");

    doc.setLineWidth(0.5);
    
    for (let i = 0; i <= 24; i++) {
        let y = gridTop + (i * 60) * (gridH / 1440.0);
        doc.setDrawColor(204, 204, 204);
        doc.line(rPage.left + timeColW, y, rPage.right, y);
        
        if (i > 0 && i < 24) {
            doc.setFont("helvetica", "normal");
            doc.setFontSize(9);
            doc.setTextColor(112, 117, 122);
            doc.text(minToTimeStr(i * 60), rPage.left + timeColW - 6, y + 3, { align: "right" });
        }
        
        if (i < 24) {
            let hy = gridTop + (i * 60 + 30) * (gridH / 1440.0);
            doc.setDrawColor(232, 232, 232); 
            doc.line(rPage.left + timeColW, hy, rPage.right, hy);
        }
    }

    doc.setDrawColor(204, 204, 204);
    for (let c = 0; c <= activeCols; c++) {
        let x = rPage.left + timeColW + (c * dayColW);
        doc.line(x, rPage.top + titleH, x, rPage.bottom);
        
        if (c < activeCols) {
            let hdr = "";
            if (isPeopleView()) {
                let shortName = people[visiblePeopleIndices[c]];
                let contactMatch = contacts.find(contact => contact.username === shortName);
                hdr = (contactMatch && (contactMatch.fn || contactMatch.fullname)) ? (contactMatch.fn || contactMatch.fullname) : shortName;
            } else {
                let d = new Date(currentDate); 
                d.setDate(d.getDate() + c); 
                hdr = d.toLocaleDateString(undefined, {weekday:'short', month:'numeric', day:'numeric'});
            }
            
            doc.setFont("helvetica", "bold");
            doc.setFontSize(11);
            doc.setTextColor(60, 64, 67);
            doc.text(hdr, x + (dayColW/2), rPage.top + titleH + 16, { align: "center" });
        }
    }

    let displayEvents = typeof unrollEvents === 'function' ? unrollEvents() : events;
    displayEvents.forEach(ev => {
        let colIdx = -1;
        if (isPeopleView()) { 
            if (ev.date === formatDate(currentDate)) {
                let dispIdx = visiblePeopleIndices.indexOf(ev.personIdx);
                if (dispIdx !== -1) colIdx = dispIdx;
            }
        } else {
            let evDate = new Date(ev.date.replace(/\//g, '-') + 'T12:00:00');
            let diffDays = Math.round((evDate.getTime() - new Date(currentDate.getFullYear(), currentDate.getMonth(), currentDate.getDate(), 12, 0, 0).getTime()) / 86400000);
            if (diffDays >= 0 && diffDays < activeCols) colIdx = diffDays;
        }

        if (colIdx >= 0 && colIdx < activeCols) {
            let top = gridTop + (ev.startMin * (gridH / 1440.0));
            let bot = gridTop + ((ev.startMin + ev.duration) * (gridH / 1440.0));
            let left = rPage.left + timeColW + (colIdx * dayColW) + 2;
            let right = left + dayColW - 4;
            let ew = right - left;
            let eh = bot - top;
            
            doc.setFillColor(ev.color);
            doc.roundedRect(left, top, ew, eh, 3, 3, "F");
            
            if (eh > 18) {
                doc.setFont("helvetica", "bold");
                doc.setFontSize(9);
                doc.setTextColor(255, 255, 255);
                
                let titleStr = ev.title.length > 20 ? ev.title.substring(0, 17) + "..." : ev.title;
                doc.text(titleStr, left + 4, top + 10, { maxWidth: ew - 8 });
                
                doc.setFont("helvetica", "normal");
                doc.text(`${minToTimeStr(ev.startMin)} - ${minToTimeStr(ev.startMin+ev.duration)}`, left + 4, top + 20);
            }
        }
    });

    let pdfName = "Schedule.pdf";
    if (visiblePeopleIndices.length === 1) {
        let shortName = people[visiblePeopleIndices[0]] || currentUser || "Calendar";
        let contactMatch = contacts.find(c => c.username === shortName);
        let finalName = (contactMatch && (contactMatch.fn || contactMatch.fullname)) ? (contactMatch.fn || contactMatch.fullname) : shortName;
        pdfName = finalName.replace(/[^a-z0-9]/gi, '_') + "_Schedule.pdf";
    } else if (currentUser) {
        pdfName = currentUser.replace(/[^a-z0-9]/gi, '_') + "_Schedule.pdf";
    }
    doc.save(pdfName);
});

document.getElementById('btnExport')?.addEventListener('click', () => {
    let content = "========================================\r\n           UPCOMING SCHEDULE            \r\n========================================\r\n\r\n";
    let displayEvents = typeof unrollEvents === 'function' ? unrollEvents() : events;
    let sorted = displayEvents.filter(e => e.date >= formatDate(new Date())).sort((a,b) => a.date.localeCompare(b.date) || a.startMin - b.startMin);
    
    sorted.forEach(e => {
        let p = people[e.personIdx] || "";
        content += `${e.title}\r\n${e.date}       ${minToTimeStr(e.startMin)} - ${minToTimeStr(e.startMin+e.duration)}       ${p}\r\n----------------------------------------\r\n`;
    });
    
    let blob = new Blob([content], {type: "text/plain"});
    let url = URL.createObjectURL(blob);
    let a = document.createElement('a');
    a.href = url;
    a.download = "Upcoming_Schedule.txt";
    a.click();
    URL.revokeObjectURL(url);
});

updateLayout();
downloadUpdates();