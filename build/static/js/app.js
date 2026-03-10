/* ============================================================
   Library Management System — SPA Application
   ============================================================ */

const API = '/api';
let currentUser = null;
let currentPage = 'dashboard';

// ============================================================
// API Helper
// ============================================================
async function api(path, opts = {}) {
    const token = localStorage.getItem('lms_token');
    const headers = { 'Content-Type': 'application/json', ...(opts.headers || {}) };
    if (token) headers['Authorization'] = `Bearer ${token}`;

    const res = await fetch(`${API}${path}`, { ...opts, headers });
    if (res.status === 401) { logout(); return null; }

    const contentType = res.headers.get('content-type') || '';
    if (contentType.includes('text/csv')) {
        const blob = await res.blob();
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = path.split('/').pop() + '.csv';
        a.click();
        URL.revokeObjectURL(url);
        return null;
    }

    const data = contentType.includes('json') ? await res.json() : null;
    if (!res.ok) {
        const msg = data?.error || `Request failed (${res.status})`;
        showToast(msg, 'error');
        throw new Error(msg);
    }
    return data;
}

// ============================================================
// Auth
// ============================================================
function isLoggedIn() { return !!localStorage.getItem('lms_token'); }

async function login(username, password) {
    const data = await api('/auth/login', {
        method: 'POST',
        body: JSON.stringify({ username, password })
    });
    if (data?.token) {
        localStorage.setItem('lms_token', data.token);
        currentUser = data.user;
        localStorage.setItem('lms_user', JSON.stringify(data.user));
        return true;
    }
    return false;
}

function logout() {
    localStorage.removeItem('lms_token');
    localStorage.removeItem('lms_user');
    currentUser = null;
    document.getElementById('login-overlay').classList.add('active');
    document.getElementById('login-username').value = '';
    document.getElementById('login-password').value = '';
}

function loadUser() {
    const u = localStorage.getItem('lms_user');
    if (u) currentUser = JSON.parse(u);
}

function isStaff() {
    return currentUser && ['librarian', 'branch_manager', 'admin'].includes(currentUser.role);
}

function isAdmin() {
    return currentUser && currentUser.role === 'admin';
}

// ============================================================
// Toast
// ============================================================
function showToast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    const icons = { success: '✅', error: '❌', warning: '⚠️', info: 'ℹ️' };
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.innerHTML = `<span>${icons[type] || ''}</span><span class="toast-message">${message}</span>`;
    container.appendChild(toast);
    setTimeout(() => { toast.style.opacity = '0'; toast.style.transform = 'translateX(100px)'; setTimeout(() => toast.remove(), 300); }, 4000);
}

// ============================================================
// Navigation
// ============================================================
const navConfig = [
    { id: 'dashboard', icon: '📊', label: 'Dashboard', roles: ['librarian', 'branch_manager', 'admin'] },
    { id: 'circulation', icon: '🔄', label: 'Circulation', roles: ['librarian', 'branch_manager', 'admin'], section: 'Operations' },
    { id: 'holds', icon: '📋', label: 'Holds Management', roles: ['librarian', 'branch_manager', 'admin'], section: 'Operations' },
    { id: 'search', icon: '🔍', label: 'Search Catalog', roles: ['member', 'librarian', 'branch_manager', 'admin'] },
    { id: 'members', icon: '👥', label: 'Members', roles: ['librarian', 'branch_manager', 'admin'], section: 'Management' },
    { id: 'catalog', icon: '📖', label: 'Catalog', roles: ['librarian', 'branch_manager', 'admin'] },
    { id: 'my-account', icon: '👤', label: 'My Account', roles: ['member'] },
    { id: 'reports', icon: '📈', label: 'Reports', roles: ['librarian', 'branch_manager', 'admin'], section: 'Analytics' },
    { id: 'settings', icon: '⚙️', label: 'Settings', roles: ['admin'], section: 'System' },
];

function buildNav() {
    const menu = document.getElementById('nav-menu');
    if (!currentUser) return;

    let html = '';
    let lastSection = '';
    navConfig.forEach(item => {
        if (!item.roles.includes(currentUser.role)) return;
        if (item.section && item.section !== lastSection) {
            html += `<div class="nav-section-label">${item.section}</div>`;
            lastSection = item.section;
        }
        html += `<a class="nav-item${item.id === currentPage ? ' active' : ''}" data-page="${item.id}">
            <span class="nav-icon">${item.icon}</span>
            <span>${item.label}</span>
        </a>`;
    });
    menu.innerHTML = html;

    menu.querySelectorAll('.nav-item').forEach(el => {
        el.addEventListener('click', () => navigateTo(el.dataset.page));
    });

    // Update user info
    const name = currentUser.username || 'User';
    document.getElementById('user-name').textContent = name;
    document.getElementById('user-role').textContent = currentUser.role?.replace('_', ' ') || '';
    document.getElementById('user-avatar').textContent = name.charAt(0).toUpperCase();
}

function navigateTo(page) {
    currentPage = page;
    location.hash = page;
    buildNav();
    renderPage(page);
}

// ============================================================
// Page Router
// ============================================================
async function renderPage(page) {
    const content = document.getElementById('page-content');
    content.innerHTML = '<div class="loading-page"><div class="loading-spinner"></div><span>Loading...</span></div>';

    try {
        switch (page) {
            case 'dashboard': await renderDashboard(content); break;
            case 'circulation': renderCirculation(content); break;
            case 'holds': await renderHolds(content); break;
            case 'search': await renderSearch(content); break;
            case 'members': await renderMembers(content); break;
            case 'catalog': await renderCatalog(content); break;
            case 'my-account': await renderMyAccount(content); break;
            case 'reports': await renderReports(content); break;
            case 'settings': await renderSettings(content); break;
            default: content.innerHTML = '<div class="empty-state"><div class="empty-icon">🚧</div><h3>Page not found</h3></div>';
        }
    } catch (err) {
        console.error('Page render error:', err);
        content.innerHTML = `<div class="empty-state"><div class="empty-icon">❌</div><h3>Error loading page</h3><p>${err.message}</p></div>`;
    }
}

// ============================================================
// DASHBOARD
// ============================================================
async function renderDashboard(el) {
    const data = await api('/dashboard');
    if (!data) return;
    const s = data.stats || {};

    el.innerHTML = `
        <div class="page-header">
            <div><h1 class="page-title">Dashboard</h1><p class="page-subtitle">Welcome back, ${currentUser.username}</p></div>
        </div>
        <div class="stats-grid">
            <div class="stat-card info"><div class="stat-icon">📚</div><div class="stat-value">${s.active_loans || 0}</div><div class="stat-label">Active Loans</div></div>
            <div class="stat-card danger"><div class="stat-icon">⏰</div><div class="stat-value">${s.overdue_items || 0}</div><div class="stat-label">Overdue Items</div></div>
            <div class="stat-card warning"><div class="stat-icon">📋</div><div class="stat-value">${s.pending_holds || 0}</div><div class="stat-label">Pending Holds</div></div>
            <div class="stat-card success"><div class="stat-icon">✅</div><div class="stat-value">${s.ready_holds || 0}</div><div class="stat-label">Ready for Pickup</div></div>
            <div class="stat-card"><div class="stat-icon">👥</div><div class="stat-value">${s.active_members || 0}</div><div class="stat-label">Active Members</div></div>
            <div class="stat-card info"><div class="stat-icon">📖</div><div class="stat-value">${s.total_items || 0}</div><div class="stat-label">Catalog Items</div></div>
            <div class="stat-card success"><div class="stat-icon">📗</div><div class="stat-value">${s.available_copies || 0}</div><div class="stat-label">Available Copies</div></div>
            <div class="stat-card"><div class="stat-icon">📊</div><div class="stat-value">${s.today_checkouts || 0}</div><div class="stat-label">Today's Checkouts</div></div>
        </div>
        <div class="card">
            <div class="card-header"><h3 class="card-title">Recent Activity</h3></div>
            <div class="table-container">
                <table>
                    <thead><tr><th>Title</th><th>Member</th><th>Barcode</th><th>Checkout</th><th>Due Date</th><th>Status</th></tr></thead>
                    <tbody>
                        ${(data.recent_activity || []).map(r => `<tr>
                            <td class="fw-600">${r.title || ''}</td>
                            <td>${r.member_name || ''}</td>
                            <td><code>${r.barcode || ''}</code></td>
                            <td class="text-sm">${r.checkout_date || ''}</td>
                            <td class="text-sm">${r.due_date || ''}</td>
                            <td>${statusBadge(r.status)}</td>
                        </tr>`).join('') || '<tr><td colspan="6" class="text-muted text-sm" style="text-align:center;padding:2rem">No recent activity</td></tr>'}
                    </tbody>
                </table>
            </div>
        </div>`;
}

// ============================================================
// CIRCULATION
// ============================================================
function renderCirculation(el) {
    el.innerHTML = `
        <div class="page-header"><div><h1 class="page-title">Circulation</h1><p class="page-subtitle">Check out & check in items</p></div></div>
        <div class="tabs">
            <div class="tab active" data-tab="checkout">📤 Check Out</div>
            <div class="tab" data-tab="checkin">📥 Check In</div>
        </div>
        <div id="circ-content"></div>`;

    el.querySelectorAll('.tab').forEach(tab => {
        tab.addEventListener('click', () => {
            el.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            tab.classList.add('active');
            if (tab.dataset.tab === 'checkout') renderCheckoutTab();
            else renderCheckinTab();
        });
    });
    renderCheckoutTab();
}

function renderCheckoutTab() {
    const container = document.getElementById('circ-content');
    container.innerHTML = `
        <div class="workflow-box" id="checkout-box">
            <div class="workflow-icon">👤</div>
            <h3>Step 1: Enter Member ID</h3>
            <p>Search for the member by ID or name</p>
            <div class="workflow-input">
                <input type="text" id="member-input" placeholder="Member ID or name..." autofocus>
                <button class="btn btn-primary" id="find-member-btn">Find</button>
            </div>
            <div id="member-result" class="mt-2"></div>
        </div>
        <div class="workflow-box mt-2 hidden" id="scan-box">
            <div class="workflow-icon">📖</div>
            <h3>Step 2: Scan Item Barcode</h3>
            <p>Enter or scan the item barcode to check out</p>
            <div class="workflow-input">
                <input type="text" id="barcode-input" placeholder="Item barcode...">
                <button class="btn btn-primary" id="scan-btn">Add</button>
            </div>
            <div id="checkout-items" class="checkout-items"></div>
        </div>`;

    document.getElementById('find-member-btn').addEventListener('click', findMember);
    document.getElementById('member-input').addEventListener('keypress', e => { if (e.key === 'Enter') findMember(); });
    document.getElementById('scan-btn').addEventListener('click', scanCheckout);
    document.getElementById('barcode-input').addEventListener('keypress', e => { if (e.key === 'Enter') scanCheckout(); });
}

async function findMember() {
    const input = document.getElementById('member-input').value.trim();
    if (!input) return;

    const resultEl = document.getElementById('member-result');
    try {
        const data = await api(`/members?q=${encodeURIComponent(input)}&per_page=5`);
        if (!data?.data?.length) {
            // Try by ID
            try {
                const member = await api(`/members/${input}`);
                if (member) { selectMember(member); return; }
            } catch (e) { }
            resultEl.innerHTML = '<p class="text-danger text-sm mt-1">No member found</p>';
            return;
        }
        if (data.data.length === 1) { selectMember(data.data[0]); return; }

        resultEl.innerHTML = data.data.map(m => `
            <div class="checkout-item" style="cursor:pointer" onclick="selectMemberById(${m.id})">
                <div><span class="item-title">${m.first_name} ${m.last_name}</span> <span class="item-barcode">${m.email || ''}</span></div>
                <div><span class="badge badge-${m.status === 'active' ? 'success' : 'danger'}">${m.status}</span></div>
            </div>`).join('');
    } catch (e) { resultEl.innerHTML = `<p class="text-danger text-sm">Error: ${e.message}</p>`; }
}

window.selectMemberById = async function (id) {
    try {
        const member = await api(`/members/${id}`);
        selectMember(member);
    } catch (e) { }
};

let checkoutMemberId = null;

function selectMember(member) {
    checkoutMemberId = member.id;
    document.getElementById('member-result').innerHTML = `
        <div class="checkout-item" style="border-left: 3px solid var(--success)">
            <div>
                <span class="item-title">${member.first_name} ${member.last_name}</span>
                <span class="item-barcode">ID: ${member.id} · ${member.membership_type} · Loans: ${member.active_loans || 0}</span>
            </div>
            <span class="badge badge-${member.status === 'active' ? 'success' : 'danger'}">${member.status}</span>
        </div>`;
    document.getElementById('checkout-box').classList.add('active');
    document.getElementById('scan-box').classList.remove('hidden');
    document.getElementById('barcode-input').focus();
}

async function scanCheckout() {
    const barcode = document.getElementById('barcode-input').value.trim();
    if (!barcode || !checkoutMemberId) return;

    try {
        const result = await api('/circulation/checkout', {
            method: 'POST',
            body: JSON.stringify({ member_id: checkoutMemberId, barcode })
        });
        if (result) {
            const items = document.getElementById('checkout-items');
            items.innerHTML += `
                <div class="checkout-item">
                    <div><span class="item-title">${result.title || barcode}</span> <span class="item-barcode">${result.barcode || barcode}</span></div>
                    <div><span class="item-due">Due: ${result.due_date || 'N/A'}</span></div>
                </div>`;
            showToast(`Checked out: ${result.title || barcode}`, 'success');
            document.getElementById('barcode-input').value = '';
            document.getElementById('barcode-input').focus();
        }
    } catch (e) { }
}

function renderCheckinTab() {
    const container = document.getElementById('circ-content');
    container.innerHTML = `
        <div class="workflow-box active">
            <div class="workflow-icon">📥</div>
            <h3>Check In Items</h3>
            <p>Scan or enter item barcodes to return them</p>
            <div class="workflow-input">
                <input type="text" id="checkin-barcode" placeholder="Item barcode..." autofocus>
                <button class="btn btn-success" id="checkin-btn">Return</button>
            </div>
        </div>
        <div id="checkin-results" class="mt-2"></div>`;

    document.getElementById('checkin-btn').addEventListener('click', processCheckin);
    document.getElementById('checkin-barcode').addEventListener('keypress', e => { if (e.key === 'Enter') processCheckin(); });
}

async function processCheckin() {
    const barcode = document.getElementById('checkin-barcode').value.trim();
    if (!barcode) return;

    try {
        const result = await api('/circulation/checkin', {
            method: 'POST',
            body: JSON.stringify({ barcode })
        });
        if (result) {
            const results = document.getElementById('checkin-results');
            let fineHtml = '';
            if (result.fine) {
                fineHtml = `<span class="badge badge-danger">Fine: $${result.fine.amount?.toFixed(2)} (${result.fine.days_overdue} days overdue)</span>`;
            }
            let holdHtml = result.routed_to_hold ? '<span class="badge badge-warning">→ Routed to hold</span>' : '';

            results.innerHTML = `
                <div class="checkout-item" style="border-left: 3px solid var(--success)">
                    <div><span class="item-title">${result.title || barcode}</span> <span class="item-barcode">${result.barcode || barcode}</span></div>
                    <div class="flex gap-1">${fineHtml}${holdHtml}<span class="badge badge-success">Returned</span></div>
                </div>` + results.innerHTML;

            showToast(`Returned: ${result.title || barcode}`, 'success');
            document.getElementById('checkin-barcode').value = '';
            document.getElementById('checkin-barcode').focus();
        }
    } catch (e) { }
}

// ============================================================
// SEARCH
// ============================================================
async function renderSearch(el) {
    el.innerHTML = `
        <div class="page-header"><div><h1 class="page-title">Search Catalog</h1><p class="page-subtitle">Find books, media, and other resources</p></div></div>
        <div class="search-bar">
            <div class="search-input-wrapper"><input type="text" id="search-query" placeholder="Search by title, author, ISBN, or keyword..." autofocus></div>
            <div class="search-filters">
                <select id="filter-type"><option value="">All Types</option><option value="book">Book</option><option value="magazine">Magazine</option><option value="dvd">DVD</option><option value="audiobook">Audiobook</option><option value="reference">Reference</option></select>
                <select id="filter-available"><option value="">All</option><option value="true">Available Only</option></select>
                <button class="btn btn-primary" id="search-btn">Search</button>
            </div>
        </div>
        <div id="search-results"></div>`;

    const doSearch = () => performSearch();
    document.getElementById('search-btn').addEventListener('click', doSearch);
    document.getElementById('search-query').addEventListener('keypress', e => { if (e.key === 'Enter') doSearch(); });

    // Load initial catalog
    await performSearch();
}

async function performSearch() {
    const query = document.getElementById('search-query').value.trim();
    const type = document.getElementById('filter-type').value;
    const available = document.getElementById('filter-available').value;

    let params = new URLSearchParams();
    if (query) params.set('q', query);
    if (type) params.set('item_type', type);
    if (available) params.set('available', available);
    params.set('per_page', '30');

    const resultsEl = document.getElementById('search-results');
    resultsEl.innerHTML = '<div class="loading-page"><div class="loading-spinner"></div></div>';

    try {
        const data = await api(`/search?${params}`);
        if (!data?.data?.length) {
            resultsEl.innerHTML = '<div class="empty-state"><div class="empty-icon">📭</div><h3>No results found</h3><p>Try different search terms or filters</p></div>';
            return;
        }

        resultsEl.innerHTML = `
            <p class="text-muted text-sm mb-2">${data.count} result(s) found</p>
            <div class="table-container">
                <table>
                    <thead><tr><th>Title</th><th>Author(s)</th><th>Type</th><th>Year</th><th>Available</th><th>Actions</th></tr></thead>
                    <tbody>
                        ${data.data.map(item => `<tr>
                            <td><strong>${item.title}</strong>${item.subtitle ? `<br><span class="text-muted text-sm">${item.subtitle}</span>` : ''}</td>
                            <td class="text-sm">${item.authors || '—'}</td>
                            <td>${typeBadge(item.item_type)}</td>
                            <td>${item.publication_year || '—'}</td>
                            <td>${item.available_copies !== undefined ? `<span class="${item.available_copies > 0 ? 'text-success' : 'text-danger'} fw-600">${item.available_copies}/${item.total_copies}</span>` : '—'}</td>
                            <td><button class="btn btn-ghost btn-sm" onclick="viewItem(${item.id})">View</button>${isStaff() ? `<button class="btn btn-ghost btn-sm" onclick="placeHoldModal(${item.id}, '${escHtml(item.title)}')">Hold</button>` : ''}</td>
                        </tr>`).join('')}
                    </tbody>
                </table>
            </div>`;
    } catch (e) { resultsEl.innerHTML = `<p class="text-danger">Error: ${e.message}</p>`; }
}

window.viewItem = async function (id) {
    try {
        const item = await api(`/catalog/${id}`);
        if (!item) return;

        showModal(`📖 ${item.title}`, `
            <div class="detail-grid mb-2">
                <div class="detail-item"><label>Authors</label><div class="value">${item.authors || '—'}</div></div>
                <div class="detail-item"><label>ISBN</label><div class="value">${item.isbn || '—'}</div></div>
                <div class="detail-item"><label>Publisher</label><div class="value">${item.publisher || '—'}</div></div>
                <div class="detail-item"><label>Year</label><div class="value">${item.publication_year || '—'}</div></div>
                <div class="detail-item"><label>Language</label><div class="value">${item.language || '—'}</div></div>
                <div class="detail-item"><label>Type</label><div class="value">${item.item_type}</div></div>
            </div>
            ${item.description ? `<div class="detail-item mb-2"><label>Description</label><div class="value text-sm">${item.description}</div></div>` : ''}
            ${item.subjects ? `<div class="detail-item mb-2"><label>Subjects</label><div class="value text-sm">${item.subjects}</div></div>` : ''}
            <h4 style="margin:1rem 0 0.5rem">Copies (${item.copies?.length || 0})</h4>
            <div class="table-container"><table>
                <thead><tr><th>Branch</th><th>Barcode</th><th>Shelf</th><th>Status</th></tr></thead>
                <tbody>${(item.copies || []).map(c => `<tr>
                    <td>${c.branch_name || ''}</td>
                    <td><code>${c.barcode}</code></td>
                    <td>${c.shelf_location || '—'}</td>
                    <td>${statusBadge(c.status)}</td>
                </tr>`).join('') || '<tr><td colspan="4" class="text-muted text-sm">No copies</td></tr>'}
                </tbody>
            </table></div>
            <p class="text-sm text-muted mt-1">Active holds: ${item.hold_count || 0}</p>`);
    } catch (e) { }
};

// ============================================================
// MEMBERS
// ============================================================
async function renderHolds(el) {
    const data = await api('/holds?per_page=100');
    if (!data) return;

    el.innerHTML = `
        <div class="page-header">
            <div><h1 class="page-title">Holds Management</h1><p class="page-subtitle">${data.count || 0} holds</p></div>
        </div>
        <div class="table-container"><table>
            <thead><tr><th>Hold Date</th><th>Title</th><th>Member</th><th>Branch</th><th>Position</th><th>Status</th></tr></thead>
            <tbody>
                ${(data.data || []).map(h => `<tr>
                    <td class="text-sm">${h.hold_date || '—'}</td>
                    <td class="fw-600">${h.title}</td>
                    <td>${h.first_name ? h.first_name + ' ' + h.last_name : 'ID: ' + h.member_id}</td>
                    <td>${h.branch_name || '—'}</td>
                    <td>${h.queue_position || '—'}</td>
                    <td>${statusBadge(h.status)}</td>
                </tr>`).join('') || '<tr><td colspan="6" class="text-muted" style="text-align:center">No holds</td></tr>'}
            </tbody>
        </table></div>`;
}

// ============================================================
// MEMBERS
// ============================================================
async function renderMembers(el) {
    const data = await api('/members?per_page=100');
    if (!data) return;

    el.innerHTML = `
        <div class="page-header">
            <div><h1 class="page-title">Members</h1><p class="page-subtitle">${data.count || 0} members</p></div>
            <button class="btn btn-primary" id="add-member-btn">+ Add Member</button>
        </div>
        <div class="search-bar">
            <div class="search-input-wrapper"><input type="text" id="member-search" placeholder="Search members..."></div>
        </div>
        <div class="table-container"><table>
            <thead><tr><th>Name</th><th>Email</th><th>Type</th><th>Status</th><th>Expiry</th><th>Actions</th></tr></thead>
            <tbody>
                ${(data.data || []).map(m => `<tr>
                    <td class="fw-600">${m.first_name} ${m.last_name}</td>
                    <td class="text-sm">${m.email || '—'}</td>
                    <td>${typeBadge(m.membership_type)}</td>
                    <td>${statusBadge(m.status)}</td>
                    <td class="text-sm">${m.expiry_date || '—'}</td>
                    <td><button class="btn btn-ghost btn-sm" onclick="viewMember(${m.id})">View</button></td>
                </tr>`).join('') || '<tr><td colspan="6" class="text-muted text-sm" style="text-align:center">No members</td></tr>'}
            </tbody>
        </table></div>`;

    document.getElementById('add-member-btn').addEventListener('click', showAddMemberModal);
}

function showAddMemberModal() {
    showModal('Add New Member', `
        <form id="add-member-form">
            <div class="form-row">
                <div class="form-group"><label>First Name *</label><input type="text" id="m-first" required></div>
                <div class="form-group"><label>Last Name *</label><input type="text" id="m-last" required></div>
            </div>
            <div class="form-row">
                <div class="form-group"><label>Email</label><input type="email" id="m-email"></div>
                <div class="form-group"><label>Phone</label><input type="text" id="m-phone"></div>
            </div>
            <div class="form-group"><label>Address</label><input type="text" id="m-address"></div>
            <div class="form-row">
                <div class="form-group"><label>Membership Type</label>
                    <select id="m-type"><option value="standard">Standard</option><option value="student">Student</option>
                    <option value="senior">Senior</option><option value="child">Child</option><option value="premium">Premium</option></select>
                </div>
                <div class="form-group"><label>Date of Birth</label><input type="date" id="m-dob"></div>
            </div>
            <div class="form-group"><label><input type="checkbox" id="m-create-account" style="width:auto;margin-right:0.5rem">Create login account</label></div>
            <div class="modal-footer">
                <button type="button" class="btn btn-secondary" onclick="closeModal()">Cancel</button>
                <button type="submit" class="btn btn-primary">Create Member</button>
            </div>
        </form>`, false);

    document.getElementById('add-member-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        try {
            await api('/members', {
                method: 'POST',
                body: JSON.stringify({
                    first_name: document.getElementById('m-first').value,
                    last_name: document.getElementById('m-last').value,
                    email: document.getElementById('m-email').value,
                    phone: document.getElementById('m-phone').value,
                    address: document.getElementById('m-address').value,
                    membership_type: document.getElementById('m-type').value,
                    date_of_birth: document.getElementById('m-dob').value,
                    create_account: document.getElementById('m-create-account').checked
                })
            });
            showToast('Member created', 'success');
            closeModal();
            navigateTo('members');
        } catch (e) { }
    });
}

window.viewMember = async function (id) {
    try {
        const m = await api(`/members/${id}`);
        if (!m) return;
        showModal(`${m.first_name} ${m.last_name}`, `
            <div class="detail-grid">
                <div class="detail-item"><label>ID</label><div class="value">${m.id}</div></div>
                <div class="detail-item"><label>Email</label><div class="value">${m.email || '—'}</div></div>
                <div class="detail-item"><label>Phone</label><div class="value">${m.phone || '—'}</div></div>
                <div class="detail-item"><label>Type</label><div class="value">${m.membership_type}</div></div>
                <div class="detail-item"><label>Status</label><div class="value">${statusBadge(m.status)}</div></div>
                <div class="detail-item"><label>Expiry</label><div class="value">${m.expiry_date || '—'}</div></div>
                <div class="detail-item"><label>Active Loans</label><div class="value fw-600">${m.active_loans || 0}</div></div>
                <div class="detail-item"><label>Outstanding Fines</label><div class="value ${m.outstanding_fines > 0 ? 'text-danger fw-600' : ''}">$${(m.outstanding_fines || 0).toFixed(2)}</div></div>
            </div>`);
    } catch (e) { }
};

// ============================================================
// CATALOG
// ============================================================
async function renderCatalog(el) {
    const data = await api('/catalog?per_page=100');
    if (!data) return;

    el.innerHTML = `
        <div class="page-header">
            <div><h1 class="page-title">Catalog Management</h1><p class="page-subtitle">${data.count || 0} items</p></div>
            <button class="btn btn-primary" id="add-item-btn">+ Add Item</button>
        </div>
        <div class="table-container"><table>
            <thead><tr><th>Title</th><th>Authors</th><th>ISBN</th><th>Type</th><th>Year</th><th>Actions</th></tr></thead>
            <tbody>
                ${(data.data || []).map(item => `<tr>
                    <td class="fw-600">${item.title}</td>
                    <td class="text-sm">${item.authors || '—'}</td>
                    <td class="text-sm"><code>${item.isbn || '—'}</code></td>
                    <td>${typeBadge(item.item_type)}</td>
                    <td>${item.publication_year || '—'}</td>
                    <td><button class="btn btn-ghost btn-sm" onclick="viewItem(${item.id})">View</button></td>
                </tr>`).join('')}
            </tbody>
        </table></div>`;

    document.getElementById('add-item-btn').addEventListener('click', showAddItemModal);
}

function showAddItemModal() {
    showModal('Add Catalog Item', `
        <form id="add-item-form">
            <div class="form-group"><label>Title *</label><input type="text" id="ci-title" required></div>
            <div class="form-group"><label>Authors *</label><input type="text" id="ci-authors" required></div>
            <div class="form-row">
                <div class="form-group"><label>ISBN</label><input type="text" id="ci-isbn"></div>
                <div class="form-group"><label>Type</label>
                    <select id="ci-type"><option value="book">Book</option><option value="magazine">Magazine</option>
                    <option value="dvd">DVD</option><option value="audiobook">Audiobook</option><option value="reference">Reference</option></select>
                </div>
            </div>
            <div class="form-row">
                <div class="form-group"><label>Publisher</label><input type="text" id="ci-publisher"></div>
                <div class="form-group"><label>Year</label><input type="number" id="ci-year"></div>
            </div>
            <div class="form-group"><label>Subjects / Tags</label><input type="text" id="ci-subjects" placeholder="comma separated"></div>
            <div class="form-group"><label>Description</label><textarea id="ci-desc"></textarea></div>
            <div class="modal-footer">
                <button type="button" class="btn btn-secondary" onclick="closeModal()">Cancel</button>
                <button type="submit" class="btn btn-primary">Create</button>
            </div>
        </form>`, false);

    document.getElementById('add-item-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        try {
            await api('/catalog', {
                method: 'POST',
                body: JSON.stringify({
                    title: document.getElementById('ci-title').value,
                    authors: document.getElementById('ci-authors').value,
                    isbn: document.getElementById('ci-isbn').value,
                    item_type: document.getElementById('ci-type').value,
                    publisher: document.getElementById('ci-publisher').value,
                    publication_year: document.getElementById('ci-year').value,
                    subjects: document.getElementById('ci-subjects').value,
                    description: document.getElementById('ci-desc').value
                })
            });
            showToast('Item added to catalog', 'success');
            closeModal();
            navigateTo('catalog');
        } catch (e) { }
    });
}

// ============================================================
// MY ACCOUNT (Member portal)
// ============================================================
async function renderMyAccount(el) {
    const [loans, holds, fines] = await Promise.all([
        api('/loans?status=active').catch(() => ({ data: [] })),
        api('/holds').catch(() => ({ data: [] })),
        api('/fines').catch(() => ({ data: [] }))
    ]);

    el.innerHTML = `
        <div class="page-header"><div><h1 class="page-title">My Account</h1></div></div>
        <div class="tabs">
            <div class="tab active" data-tab="loans">📚 My Loans (${loans?.data?.length || 0})</div>
            <div class="tab" data-tab="holds">📋 My Holds (${holds?.data?.length || 0})</div>
            <div class="tab" data-tab="fines">💰 Fines (${fines?.data?.length || 0})</div>
        </div>
        <div id="account-tab-content"></div>`;

    const tabs = { loans: loans?.data || [], holds: holds?.data || [], fines: fines?.data || [] };

    function showTab(tab) {
        const content = document.getElementById('account-tab-content');
        if (tab === 'loans') {
            content.innerHTML = `<div class="table-container"><table>
                <thead><tr><th>Title</th><th>Barcode</th><th>Due Date</th><th>Status</th><th>Actions</th></tr></thead>
                <tbody>${tabs.loans.map(l => `<tr>
                    <td class="fw-600">${l.title || '—'}</td>
                    <td><code>${l.barcode || ''}</code></td>
                    <td>${l.due_date || '—'}</td>
                    <td>${statusBadge(l.status)}</td>
                    <td>${l.status === 'active' ? `<button class="btn btn-sm btn-secondary" onclick="renewLoan(${l.id})">Renew</button>` : ''}</td>
                </tr>`).join('') || '<tr><td colspan="5" class="text-muted" style="text-align:center;padding:2rem">No active loans</td></tr>'}</tbody>
            </table></div>`;
        } else if (tab === 'holds') {
            content.innerHTML = `<div class="table-container"><table>
                <thead><tr><th>Title</th><th>Branch</th><th>Status</th><th>Position</th></tr></thead>
                <tbody>${tabs.holds.map(h => `<tr>
                    <td class="fw-600">${h.title || '—'}</td>
                    <td>${h.branch_name || '—'}</td>
                    <td>${statusBadge(h.status)}</td>
                    <td>${h.queue_position || '—'}</td>
                </tr>`).join('') || '<tr><td colspan="4" class="text-muted" style="text-align:center;padding:2rem">No holds</td></tr>'}</tbody>
            </table></div>`;
        } else {
            content.innerHTML = `<div class="table-container"><table>
                <thead><tr><th>Item</th><th>Amount</th><th>Paid</th><th>Status</th></tr></thead>
                <tbody>${tabs.fines.map(f => `<tr>
                    <td>${f.title || '—'}</td>
                    <td class="fw-600">$${(f.amount || 0).toFixed(2)}</td>
                    <td>$${(f.paid_amount || 0).toFixed(2)}</td>
                    <td>${statusBadge(f.status)}</td>
                </tr>`).join('') || '<tr><td colspan="4" class="text-muted" style="text-align:center;padding:2rem">No fines</td></tr>'}</tbody>
            </table></div>`;
        }
    }

    el.querySelectorAll('.tab').forEach(t => {
        t.addEventListener('click', () => {
            el.querySelectorAll('.tab').forEach(x => x.classList.remove('active'));
            t.classList.add('active');
            showTab(t.dataset.tab);
        });
    });
    showTab('loans');
}

window.renewLoan = async function (loanId) {
    try {
        await api('/circulation/renew', { method: 'POST', body: JSON.stringify({ loan_id: loanId }) });
        showToast('Loan renewed successfully', 'success');
        navigateTo(currentPage);
    } catch (e) { }
};

// ============================================================
// REPORTS
// ============================================================
async function renderReports(el) {
    el.innerHTML = `
        <div class="page-header"><div><h1 class="page-title">Reports & Analytics</h1></div></div>
        <div class="tabs">
            <div class="tab active" data-tab="circulation">📊 Circulation</div>
            <div class="tab" data-tab="top-items">🏆 Top Items</div>
            <div class="tab" data-tab="overdue">⏰ Overdue</div>
            <div class="tab" data-tab="members">👥 Members</div>
        </div>
        <div id="report-content"></div>`;

    el.querySelectorAll('.tab').forEach(t => {
        t.addEventListener('click', () => {
            el.querySelectorAll('.tab').forEach(x => x.classList.remove('active'));
            t.classList.add('active');
            loadReport(t.dataset.tab);
        });
    });
    loadReport('circulation');
}

async function loadReport(type) {
    const content = document.getElementById('report-content');
    content.innerHTML = '<div class="loading-page"><div class="loading-spinner"></div></div>';

    try {
        if (type === 'circulation') {
            const data = await api('/reports/circulation?period=30');
            const s = data?.summary || {};
            content.innerHTML = `
                <div class="flex items-center justify-between mb-2">
                    <h3>Last 30 Days</h3>
                    <button class="btn btn-sm btn-secondary" onclick="api('/reports/export/overdue')">📥 Export CSV</button>
                </div>
                <div class="stats-grid">
                    <div class="stat-card info"><div class="stat-value">${s.checkouts || 0}</div><div class="stat-label">Checkouts</div></div>
                    <div class="stat-card success"><div class="stat-value">${s.returns || 0}</div><div class="stat-label">Returns</div></div>
                    <div class="stat-card"><div class="stat-value">${s.active_loans || 0}</div><div class="stat-label">Active Loans</div></div>
                    <div class="stat-card danger"><div class="stat-value">${s.overdue || 0}</div><div class="stat-label">Overdue</div></div>
                    <div class="stat-card warning"><div class="stat-value">${s.pending_holds || 0}</div><div class="stat-label">Pending Holds</div></div>
                    <div class="stat-card"><div class="stat-value">${s.active_members || 0}</div><div class="stat-label">Active Members</div></div>
                </div>`;
        } else if (type === 'top-items') {
            const data = await api('/reports/top-items?period=90&limit=15');
            content.innerHTML = `<div class="table-container"><table>
                <thead><tr><th>#</th><th>Title</th><th>Authors</th><th>Type</th><th>Checkouts</th></tr></thead>
                <tbody>${(data?.data || []).map((item, i) => `<tr>
                    <td class="fw-600">${i + 1}</td>
                    <td class="fw-600">${item.title}</td>
                    <td class="text-sm">${item.authors || ''}</td>
                    <td>${typeBadge(item.item_type)}</td>
                    <td class="fw-600">${item.checkout_count}</td>
                </tr>`).join('') || '<tr><td colspan="5" class="text-muted" style="text-align:center">No data</td></tr>'}</tbody>
            </table></div>`;
        } else if (type === 'overdue') {
            const data = await api('/reports/overdue');
            content.innerHTML = `
                <div class="flex items-center justify-between mb-2">
                    <p class="text-muted">${data?.count || 0} overdue items</p>
                    <button class="btn btn-sm btn-secondary" onclick="api('/reports/export/overdue')">📥 Export</button>
                </div>
                <div class="table-container"><table>
                <thead><tr><th>Title</th><th>Member</th><th>Barcode</th><th>Due Date</th><th>Days Overdue</th><th>Branch</th></tr></thead>
                <tbody>${(data?.data || []).map(item => `<tr>
                    <td class="fw-600">${item.title}</td>
                    <td>${item.first_name} ${item.last_name}</td>
                    <td><code>${item.barcode}</code></td>
                    <td class="text-sm">${item.due_date}</td>
                    <td><span class="badge badge-danger">${item.days_overdue} days</span></td>
                    <td class="text-sm">${item.branch_name || ''}</td>
                </tr>`).join('') || '<tr><td colspan="6" class="text-muted" style="text-align:center">No overdue items 🎉</td></tr>'}</tbody>
            </table></div>`;
        } else if (type === 'members') {
            const data = await api('/reports/members?period=90&limit=15');
            const s = data?.summary || {};
            content.innerHTML = `
                <div class="stats-grid mb-2">
                    <div class="stat-card success"><div class="stat-value">${s.active || 0}</div><div class="stat-label">Active</div></div>
                    <div class="stat-card warning"><div class="stat-value">${s.suspended || 0}</div><div class="stat-label">Suspended</div></div>
                    <div class="stat-card danger"><div class="stat-value">${s.expired || 0}</div><div class="stat-label">Expired</div></div>
                    <div class="stat-card"><div class="stat-value">${s.total || 0}</div><div class="stat-label">Total</div></div>
                </div>
                <h3 class="mb-1">Top Borrowers (90 days)</h3>
                <div class="table-container"><table>
                <thead><tr><th>#</th><th>Name</th><th>Type</th><th>Checkouts</th></tr></thead>
                <tbody>${(data?.top_borrowers || []).map((m, i) => `<tr>
                    <td class="fw-600">${i + 1}</td>
                    <td class="fw-600">${m.first_name} ${m.last_name}</td>
                    <td>${typeBadge(m.membership_type)}</td>
                    <td class="fw-600">${m.checkout_count}</td>
                </tr>`).join('') || '<tr><td colspan="4" class="text-muted" style="text-align:center">No data</td></tr>'}</tbody>
            </table></div>`;
        }
    } catch (e) { content.innerHTML = `<p class="text-danger">Error: ${e.message}</p>`; }
}

// ============================================================
// SETTINGS
// ============================================================
async function renderSettings(el) {
    const [branches, rules] = await Promise.all([
        api('/branches').catch(() => ({ data: [] })),
        api('/circulation-rules').catch(() => ({ data: [] }))
    ]);

    el.innerHTML = `
        <div class="page-header"><div><h1 class="page-title">System Settings</h1></div></div>
        <div class="tabs">
            <div class="tab active" data-tab="branches">🏢 Branches</div>
            <div class="tab" data-tab="rules">📐 Circulation Rules</div>
        </div>
        <div id="settings-content"></div>`;

    function showSettings(tab) {
        const content = document.getElementById('settings-content');
        if (tab === 'branches') {
            content.innerHTML = `<div class="table-container"><table>
                <thead><tr><th>Name</th><th>Address</th><th>Phone</th><th>Email</th><th>Hours</th></tr></thead>
                <tbody>${(branches?.data || []).map(b => `<tr>
                    <td class="fw-600">${b.name}</td>
                    <td class="text-sm">${b.address || '—'}</td>
                    <td class="text-sm">${b.phone || '—'}</td>
                    <td class="text-sm">${b.email || '—'}</td>
                    <td class="text-sm">${b.operating_hours || '—'}</td>
                </tr>`).join('')}</tbody>
            </table></div>`;
        } else {
            content.innerHTML = `<div class="table-container"><table>
                <thead><tr><th>Member Type</th><th>Item Type</th><th>Loan Days</th><th>Max Loans</th><th>Max Renewals</th><th>Fine/Day</th><th>Fine Cap</th></tr></thead>
                <tbody>${(rules?.data || []).map(r => `<tr>
                    <td class="fw-600">${r.membership_type}</td>
                    <td>${r.item_type}</td>
                    <td>${r.loan_period_days}</td>
                    <td>${r.max_loans}</td>
                    <td>${r.max_renewals}</td>
                    <td>$${(r.fine_per_day || 0).toFixed(2)}</td>
                    <td>$${(r.fine_cap || 0).toFixed(2)}</td>
                </tr>`).join('')}</tbody>
            </table></div>`;
        }
    }

    el.querySelectorAll('.tab').forEach(t => {
        t.addEventListener('click', () => {
            el.querySelectorAll('.tab').forEach(x => x.classList.remove('active'));
            t.classList.add('active');
            showSettings(t.dataset.tab);
        });
    });
    showSettings('branches');
}

// ============================================================
// Hold Modal
// ============================================================
window.placeHoldModal = async function (itemId, title) {
    const branches = await api('/branches').catch(() => ({ data: [] }));
    showModal('Place Hold', `
        <p class="mb-2">Place a hold on: <strong>${title}</strong></p>
        ${isStaff() ? `<div class="form-group"><label>Member ID *</label><input type="number" id="hold-member" placeholder="Enter Member ID"></div>` : ''}
        <div class="form-group"><label>Pickup Branch</label>
            <select id="hold-branch">${(branches?.data || []).map(b => `<option value="${b.id}">${b.name}</option>`).join('')}</select>
        </div>
        <div class="modal-footer">
            <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
            <button class="btn btn-primary" id="place-hold-btn">Place Hold</button>
        </div>`, false);

    document.getElementById('place-hold-btn').addEventListener('click', async () => {
        const payload = {
            catalog_item_id: itemId,
            pickup_branch_id: parseInt(document.getElementById('hold-branch').value)
        };

        if (isStaff()) {
            const memberEl = document.getElementById('hold-member');
            if (memberEl && memberEl.value) {
                payload.member_id = parseInt(memberEl.value);
            } else {
                showToast('Member ID is required for staff placing holds', 'error');
                return;
            }
        }

        try {
            await api('/holds', {
                method: 'POST',
                body: JSON.stringify(payload)
            });
            showToast('Hold placed successfully', 'success');
            closeModal();
        } catch (e) { }
    });
};

// ============================================================
// Utility Functions
// ============================================================
function statusBadge(status) {
    const map = {
        active: 'success', available: 'success', returned: 'success', paid: 'success', fulfilled: 'success', ready: 'success', sent: 'success',
        overdue: 'danger', lost: 'danger', damaged: 'danger', banned: 'danger', expired: 'danger', cancelled: 'danger', unpaid: 'danger',
        on_loan: 'warning', on_hold: 'warning', pending: 'warning', partial: 'warning', suspended: 'warning', in_transit: 'info',
        withdrawn: 'neutral', waived: 'neutral'
    };
    return `<span class="badge badge-${map[status] || 'neutral'}">${(status || '').replace(/_/g, ' ')}</span>`;
}

function typeBadge(type) {
    const icons = { book: '📕', magazine: '📰', dvd: '💿', audiobook: '🎧', ebook: '📱', reference: '📚', other: '📦' };
    return `<span class="badge badge-neutral">${icons[type] || '📦'} ${type || 'other'}</span>`;
}

function escHtml(str) { return (str || '').replace(/'/g, "\\'").replace(/"/g, '&quot;'); }

// ============================================================
// Modal System
// ============================================================
function showModal(title, bodyHtml, showCloseFooter = true) {
    const existing = document.querySelector('.modal-overlay');
    if (existing) existing.remove();

    const overlay = document.createElement('div');
    overlay.className = 'modal-overlay';
    overlay.innerHTML = `
        <div class="modal">
            <div class="modal-header">
                <h2 class="modal-title">${title}</h2>
                <button class="modal-close" onclick="closeModal()">&times;</button>
            </div>
            <div class="modal-body">${bodyHtml}</div>
            ${showCloseFooter ? '<div class="modal-footer"><button class="btn btn-secondary" onclick="closeModal()">Close</button></div>' : ''}
        </div>`;
    overlay.addEventListener('click', (e) => { if (e.target === overlay) closeModal(); });
    document.body.appendChild(overlay);
}

window.closeModal = function () {
    const overlay = document.querySelector('.modal-overlay');
    if (overlay) overlay.remove();
};

// ============================================================
// Initialization
// ============================================================
document.addEventListener('DOMContentLoaded', () => {
    // Login form
    document.getElementById('login-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        const errorEl = document.getElementById('login-error');
        errorEl.style.display = 'none';
        const btn = document.getElementById('login-btn');
        btn.disabled = true;
        btn.textContent = 'Signing in...';

        try {
            const ok = await login(
                document.getElementById('login-username').value,
                document.getElementById('login-password').value
            );
            if (ok) {
                document.getElementById('login-overlay').classList.remove('active');
                buildNav();
                const defaultPage = isStaff() ? 'dashboard' : (currentUser.role === 'member' ? 'search' : 'dashboard');
                navigateTo(location.hash.slice(1) || defaultPage);
            } else {
                errorEl.textContent = 'Invalid username or password';
                errorEl.style.display = 'block';
            }
        } catch (err) {
            errorEl.textContent = err.message || 'Login failed';
            errorEl.style.display = 'block';
        }
        btn.disabled = false;
        btn.textContent = 'Sign In';
    });

    // Logout
    document.getElementById('btn-logout').addEventListener('click', logout);

    // Check if already logged in
    if (isLoggedIn()) {
        loadUser();
        document.getElementById('login-overlay').classList.remove('active');
        buildNav();
        const page = location.hash.slice(1) || (isStaff() ? 'dashboard' : 'search');
        navigateTo(page);
    }

    // Hash routing
    window.addEventListener('hashchange', () => {
        const page = location.hash.slice(1);
        if (page && page !== currentPage) navigateTo(page);
    });
});
