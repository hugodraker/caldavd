const hostname = window.location.hostname || "127.0.0.1";
const CALDAV_URL = window.location.origin + '/calendar/';
const CONFIG_URL = window.location.origin + '/config/';
let zoom = 1.0;
let viewMode = 3; 
let currentDate = new Date();
let selectedEventId = null;
let events = [];

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

function renderTimeline() {
    if (!gridBg || !timelineLabels || !headerRow) return;
    
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
            div.innerText = people[pIdx] || `Person ${pIdx + 1}`;
            div.ondblclick = () => {
                let input = document.createElement('input');
                input.className = 'edit-person-input';
                input.value = people[pIdx] || "";
                div.innerHTML = '';
                div.appendChild(input);
                input.focus();
                
                input.onblur = input.onkeydown = (ev) => {
                    if (ev.type === 'keydown' && ev.key !== 'Enter') return;
                    input.onblur = null; input.onkeydown = null;
                    people[pIdx] = input.value || `Person ${pIdx + 1}`;
                    putPeopleToCalDAV();
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

    events.forEach(ev => {
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
                    ev.title = input.value || "Untitled Event";
                    putEventToCalDAV(ev);
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
            
            events.filter(e => e.date === cellDateStr).forEach(ev => {
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
                        ev.title = input.value || "Untitled Event";
                        putEventToCalDAV(ev);
                        window.isEditing = false;
                        render();
                    };
                };

                cell.appendChild(evDiv);
            });
            
            cell.ondblclick = () => {
                let myIdx = people.indexOf(currentUser);
                if (myIdx === -1) myIdx = 0;
                let newEv = { id: Date.now().toString(), title: "New Event", startMin: 540, duration: 60, color: colors[events.length % colors.length], date: cellDateStr, personIdx: myIdx };
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
    monthDragEv = events.find(x => x.id === selectedEventId);
    
    if (!canEdit(monthDragEv)) {
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
    
    let sorted = events.filter(e => e.date >= formatDate(new Date())).sort((a,b) => a.date.localeCompare(b.date) || a.startMin - b.startMin);
    
    sorted.forEach(ev => {
        let card = document.createElement('div');
        card.className = 'upcoming-card';
        let p = people[ev.personIdx] || "";
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
    render();
    updateLayout();
});

function handleDrag(e) {
    if (e.target.classList.contains('resize-handle') || e.target.tagName === 'INPUT') return;
    if (window.isEditing) return;
    
    selectedEventId = e.currentTarget.dataset.id;
    activeEv = events.find(ev => ev.id === selectedEventId);
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
    activeEv = events.find(ev => ev.id === selectedEventId);
    
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
                // Ignore horizontal drag if they aren't allowed to assign to others
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
        if (pIdx === -1 || pIdx === undefined) pIdx = people.indexOf(currentUser);
        
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
    document.getElementById('btnFilterPeople').style.display = (viewMode === 4) ? 'inline-block' : 'none';
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
        let ev = events.find(e => e.id === selectedEventId);
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
        let evToDel = events.find(e => e.id === selectedEventId);
        if (evToDel && !canEdit(evToDel)) { alert("You do not have permission to delete this event."); return; }
        
        events = events.filter(e => e.id !== selectedEventId);
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
    saveVisiblePeople();
    document.getElementById('peopleFilterModal').style.display = 'none';
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
    
    let titleH = 32, subH = 25;
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
            let hdr = isPeopleView() ? people[visiblePeopleIndices[c]] : (() => { let d = new Date(currentDate); d.setDate(d.getDate() + c); return d.toLocaleDateString(undefined, {weekday:'short', month:'numeric', day:'numeric'}); })();
            doc.setFont("helvetica", "bold");
            doc.setFontSize(11);
            doc.setTextColor(60, 64, 67);
            doc.text(hdr, x + (dayColW/2), rPage.top + titleH + 16, { align: "center" });
        }
    }

    events.forEach(ev => {
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

    doc.save("Calendar_Schedule_Vector.pdf");
});

document.getElementById('btnExport')?.addEventListener('click', () => {
    let content = "========================================\r\n           UPCOMING SCHEDULE            \r\n========================================\r\n\r\n";
    let sorted = events.filter(e => e.date >= formatDate(new Date())).sort((a,b) => a.date.localeCompare(b.date) || a.startMin - b.startMin);
    
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