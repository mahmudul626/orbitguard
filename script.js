document.addEventListener('DOMContentLoaded', () => {
    // --- AUTHENTICATION CHECK ---
    const token = localStorage.getItem('orbitGuardToken');
    const user = JSON.parse(localStorage.getItem('orbitGuardUser'));

    if (!token || !user) {
        window.location.href = 'login.html';
        return;
    }

    const SERVER_URL = 'http://localhost:8080';

    // --- UI Element References ---
    const vizSubtitle = document.getElementById('viz-subtitle');
    const dataSubtitle = document.getElementById('data-subtitle');
    const initialMessageViz = document.getElementById('initial-message-viz');
    const recommendationCard = document.getElementById('recommendation-card');
    const chartCanvas = document.getElementById('densityChart');
    const dataOutput = document.getElementById('data-output');
    
    const sidebar = document.getElementById('sidebar');
    const mainContent = document.getElementById('main-content');
    const sidebarToggleBtn = document.getElementById('sidebar-toggle-btn');
    const footer = document.getElementById('footer');

    // --- MODAL Element References ---
    const detailsModal = document.getElementById('details-modal');
    const modalCloseBtn = document.getElementById('modal-close-btn');
    const modalTitle = document.getElementById('modal-title');
    const modalBody = document.getElementById('modal-body');

    let densityChart = null;
    let currentApiController = null;

    // --- Tab and Page Switching Logic ---
    const pages = ['dashboardPage', 'satellitesPage', 'plannerPage', 'upgradePage', 'apiKeyPage'];
    const tabs = {
        dashboardTab: 'dashboardPage',
        satellitesTab: 'satellitesPage',
        plannerTab: 'plannerPage'
    };

    function showPage(pageId) {
        pages.forEach(p => {
            const pageEl = document.getElementById(p);
            if (pageEl) pageEl.classList.add('hidden');
        });
        const pageToShow = document.getElementById(pageId);
        if (pageToShow) {
            pageToShow.classList.remove('hidden');
        }
    }

    function deselectAllTabs() {
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.classList.remove('bg-green-500/20', 'text-green-400', 'border-green-500/50');
            btn.classList.add('hover:bg-gray-700/50');
        });
    }

    Object.keys(tabs).forEach(tabId => {
        const tabButton = document.getElementById(tabId);
        if (tabButton) {
            tabButton.addEventListener('click', () => {
                showPage(tabs[tabId]);
                deselectAllTabs();
                tabButton.classList.add('bg-green-500/20', 'text-green-400', 'border-green-500/50');
                tabButton.classList.remove('hover:bg-gray-700/50');
            });
        }
    });

    // --- SIDEBAR LOGIC ---
    sidebarToggleBtn.addEventListener('click', () => {
        sidebar.classList.toggle('active');
        mainContent.classList.toggle('collapsed');
        footer.classList.toggle('collapsed');
    });

    document.getElementById('sidebar-upgrade-btn').addEventListener('click', () => {
        showPage('upgradePage');
        deselectAllTabs();
    });
    document.getElementById('sidebar-apikey-btn').addEventListener('click', () => {
        showPage('apiKeyPage');
        deselectAllTabs();
    });

    document.getElementById('sidebar-logout-btn').addEventListener('click', () => {
        localStorage.removeItem('orbitGuardToken');
        localStorage.removeItem('orbitGuardUser');
        window.location.href = 'login.html';
    });
    
    document.querySelectorAll(".quick-action-btn").forEach(btn => {
        btn.addEventListener("click", () => {
            const targetPage = btn.getAttribute("data-target");
            const targetTabId = Object.keys(tabs).find(key => tabs[key] === targetPage);
            if (targetTabId) {
                document.getElementById(targetTabId).click();
            }
        });
    });

    // --- FEATURE GATING & UI UPDATES ---
    function updateUpgradePageUI() {
        const currentUser = JSON.parse(localStorage.getItem('orbitGuardUser'));
        const isPro = currentUser && currentUser.plan === 'pro';
        const btnGetFree = document.getElementById('btnGetFree');
        const btnGetPro = document.getElementById('btnGetPro');
        const upgradeMsg = document.getElementById('upgrade-message');

        upgradeMsg.textContent = '';

        if (isPro) {
            btnGetPro.textContent = 'Your Current Version';
            btnGetPro.disabled = true;
            btnGetPro.classList.add('cursor-not-allowed', 'bg-gray-600');
            btnGetPro.classList.remove('hover:bg-green-700');
            btnGetFree.textContent = 'Switch to Free';
            btnGetFree.disabled = false;
            btnGetFree.classList.remove('cursor-not-allowed', 'bg-gray-600');
            btnGetFree.classList.add('hover:bg-gray-700');
        } else {
            btnGetFree.textContent = 'Your Current Version';
            btnGetFree.disabled = true;
            btnGetFree.classList.add('cursor-not-allowed', 'bg-gray-600');
            btnGetFree.classList.remove('hover:bg-gray-700');
            btnGetPro.textContent = 'Get Pro';
            btnGetPro.disabled = false;
            btnGetPro.classList.remove('cursor-not-allowed', 'bg-gray-600');
            btnGetPro.classList.add('hover:bg-green-700');
        }
    }

    function applyFeatureGating() {
        const currentUser = JSON.parse(localStorage.getItem('orbitGuardUser'));
        const isPro = currentUser && currentUser.plan === 'pro';
        const btnPredict = document.getElementById('btnPredict');
        const btnPlan = document.getElementById('btnPlan');
        const sidebarApiKeyBtn = document.getElementById('sidebar-apikey-btn');
        
        if (!isPro) {
            if (btnPredict) {
                btnPredict.disabled = true;
                btnPredict.classList.add('disabled-btn');
                btnPredict.parentElement.title = "Upgrade to Pro to use this feature.";
            }
            if (btnPlan) {
                btnPlan.disabled = true;
                btnPlan.classList.add('disabled-btn');
                btnPlan.parentElement.title = "Upgrade to Pro to use this feature.";
            }
            if (sidebarApiKeyBtn) {
                sidebarApiKeyBtn.disabled = true;
                sidebarApiKeyBtn.classList.add('disabled-icon');
                sidebarApiKeyBtn.parentElement.setAttribute('data-tooltip', 'API Key (Pro Only)');
            }
        } else {
            if (btnPredict) {
                btnPredict.disabled = false;
                btnPredict.classList.remove('disabled-btn');
                btnPredict.parentElement.title = "";
            }
            if (btnPlan) {
                btnPlan.disabled = false;
                btnPlan.classList.remove('disabled-btn');
                btnPlan.parentElement.title = "";
            }
            if (sidebarApiKeyBtn) {
                sidebarApiKeyBtn.disabled = false;
                sidebarApiKeyBtn.classList.remove('disabled-icon');
                sidebarApiKeyBtn.parentElement.setAttribute('data-tooltip', 'API Key');
            }
        }
        updateUpgradePageUI();
    }
    applyFeatureGating();

    // --- HELPER FUNCTIONS ---
    function showLoader(area) {
        if (area === 'viz') {
            initialMessageViz.innerHTML = `<div class="text-green-400">Analyzing Data...</div>`;
            initialMessageViz.style.display = 'flex';
            if (densityChart) densityChart.destroy();
            chartCanvas.style.display = 'none';
            recommendationCard.style.display = 'none';
        } else {
            dataOutput.innerHTML = `<div class="text-yellow-400 text-center p-4">Fetching from Server...</div>`;
        }
    }

    function clearUI(isPlanner = false) {
        if (isPlanner) {
            vizSubtitle.textContent = "Results will be charted here.";
            initialMessageViz.innerHTML = '<p class="text-gray-500">Enter a target altitude and click \'Analyze Path\'.</p>';
            initialMessageViz.style.display = 'flex';
            if (densityChart) densityChart.destroy();
            chartCanvas.style.display = 'none';
            recommendationCard.style.display = 'none';
        } else {
            dataSubtitle.textContent = "Results from Mission Control queries will be shown here.";
            dataOutput.innerHTML = '<p class="text-gray-500">Run a query to see the data.</p>';
        }
    }

    // --- DISPLAY FUNCTIONS ---
    function displayListOrFilter(data, title) {
        dataSubtitle.textContent = `Displaying ${data.satellites.length} objects for: ${title}. Click on a name for details.`;
        if (!data.satellites || data.satellites.length === 0) {
            dataOutput.innerHTML = `<p class="text-yellow-400">No satellites found for this query.</p>`;
            return;
        }
        let tableHTML = `<table class="w-full text-left text-sm">
            <thead class="text-green-400"><tr><th class="p-2">Name</th><th class="p-2">Altitude (km)</th><th class="p-2">NORAD ID</th></tr></thead><tbody>`;
        data.satellites.forEach(sat => {
            // MODIFIED: Name is now a link-like element with data attributes to trigger the modal
            tableHTML += `<tr class="border-t border-gray-700/50 hover:bg-gray-700/30">
                <td class="p-2"><a href="#" class="satellite-link" data-norad="${sat.norad_id}" data-name="${sat.name}">${sat.name}</a></td>
                <td class="p-2">${sat.altitude.toFixed(2)}</td>
                <td class="p-2">${sat.norad_id}</td>
            </tr>`;
        });
        tableHTML += `</tbody></table>`;
        dataOutput.innerHTML = tableHTML;
    }

    function displayRiskCheck(data, targetAlt, tolerance) {
        dataSubtitle.textContent = `Risk check results for ${targetAlt}km ± ${tolerance}km. Click on a name for details.`;
        if (!data.risk_found || data.risks.length === 0) {
             dataOutput.innerHTML = `<p class="text-green-400">✅ No immediate risks found in the specified range.</p>`;
             return;
        }
        let listHTML = `<div class="text-red-400 mb-2">🚨 Found ${data.risks.length} potential risk(s):</div><ul class="space-y-2">`;
        data.risks.forEach(risk => {
            // MODIFIED: Name is now a link-like element with data attributes
            listHTML += `<li class="p-2 bg-red-900/30 border border-red-500/30 rounded">
                <strong><a href="#" class="satellite-link" data-norad="${risk.norad_id}" data-name="${risk.name}">${risk.name}</a></strong> at ${risk.altitude.toFixed(2)} km
            </li>`;
        });
        listHTML += `</ul>`;
        dataOutput.innerHTML = listHTML;
    }
    
    function displayPredictions(data) {
        if(data.error){
            dataOutput.innerHTML = `<p class="text-red-400">${data.error}</p>`;
            return;
        }
        dataSubtitle.textContent = `Found ${data.events.length} potential close approaches.`;
        if (data.events.length === 0) {
            dataOutput.innerHTML = `<p class="text-green-400">✅ No high-risk collision events predicted.</p>`;
            return;
        }
        let cardsHTML = `<div class="space-y-3">`;
        data.events.forEach(event => {
            cardsHTML += `<div class="p-3 bg-yellow-900/30 border border-yellow-500/30 rounded-lg">
                <div class="font-bold text-yellow-300">Close Approach Event</div>
                <div class="text-sm">
                    <p><span class="text-gray-400">Object 1:</span> ${event.object1_name}</p>
                    <p><span class="text-gray-400">Object 2:</span> ${event.object2_name}</p>
                    <p><span class="text-gray-400">Min. Distance:</span> <span class="text-red-400 font-bold">${event.min_distance_km.toFixed(2)} km</span></p>
                    <p><span class="text-gray-400">Time from now:</span> ${event.time_from_now_hr.toFixed(1)} hours</p>
                </div></div>`;
        });
        cardsHTML += `</div>`;
        dataOutput.innerHTML = cardsHTML;
    }

    function displayPlannerResults(data) {
         if(data.error){
            initialMessageViz.innerHTML = `<p class="text-red-400">${data.error}</p>`;
            initialMessageViz.style.display = 'flex';
            return;
        }
        vizSubtitle.textContent = "Orbital Density Analysis";
        initialMessageViz.style.display = 'none';
        chartCanvas.style.display = 'block';
        recommendationCard.style.display = 'block';

        const { analysis, recommendation } = data;
        const labels = analysis.map(bin => `${bin.alt_start_km}-${bin.alt_end_km}`);
        const objectCounts = analysis.map(bin => bin.object_count);
        const backgroundColors = analysis.map(bin => {
            if (bin.is_target_bin) return 'rgba(239, 68, 68, 0.6)';
            if (bin.alt_start_km === recommendation.safe_alt_start_km) return 'rgba(34, 197, 94, 0.6)';
            return 'rgba(59, 130, 246, 0.6)';
        });

        if (densityChart) densityChart.destroy();
        densityChart = new Chart(chartCanvas.getContext('2d'), {
            type: 'bar',
            data: { labels, datasets: [{ label: '# of Objects', data: objectCounts, backgroundColor: backgroundColors, borderWidth: 1 }] },
            options: {
                maintainAspectRatio: false,
                scales: {
                    y: { beginAtZero: true, title: { display: true, text: 'Object Count', color: '#9ca3af' }, grid: { color: 'rgba(156, 163, 175, 0.2)'}, ticks: { color: '#d1d5db' }},
                    x: { title: { display: true, text: 'Altitude Band (km)', color: '#9ca3af' }, grid: { display: false }, ticks: { color: '#d1d5db' }}
                },
                plugins: { legend: { display: false }, title: { display: true, text: 'Orbital Density Analysis', color: '#fff', font: { size: 16, family: 'Orbitron' }}}
            }
        });
        
        recommendationCard.innerHTML = `<h3 class="font-bold text-green-400 orbitron">✅ Recommendation</h3>
            <p>Safest band is <strong>${recommendation.safe_alt_start_km}-${recommendation.safe_alt_end_km} km</strong>, with only <strong>${recommendation.object_count}</strong> objects.</p>`;
    }

    // --- API FETCH LOGIC ---
    async function fetchAPI(endpoint, body, title) {
        if (currentApiController) currentApiController.abort();
        currentApiController = new AbortController();
        
        const isPlanner = endpoint === '/plan';
        clearUI(isPlanner);
        showLoader(isPlanner ? 'viz' : 'data');

        try {
            const authenticatedBody = { ...body, email: user.email, token: token };

            const response = await fetch(`${SERVER_URL}${endpoint}`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(authenticatedBody),
                signal: currentApiController.signal
            });

            if (response.status === 401 || response.status === 403) {
                const errData = await response.json();
                throw new Error(errData.error || `Request failed with status ${response.status}`);
            }
             if (!response.ok) throw new Error(`HTTP error! Status: ${response.status}`);
            
            const data = await response.json();

            if (isPlanner) displayPlannerResults(data);
            else if (endpoint === '/list' || endpoint === '/filter') displayListOrFilter(data, title);
            else if (endpoint === '/risk') displayRiskCheck(data, body.target_alt, body.tolerance);
            else if (endpoint === '/predict') displayPredictions(data);

        } catch (error) {
            if (error.name !== 'AbortError') {
                if (error.message.includes("Authentication failed")) {
                    alert("Your session has expired. Please log in again.");
                    localStorage.removeItem('orbitGuardToken');
                    localStorage.removeItem('orbitGuardUser');
                    window.location.href = 'login.html';
                    return;
                }
                const errorMsg = `<div class="text-red-500 text-center p-4"><strong>Error:</strong> ${error.message}</div>`;
                if(isPlanner) {
                    initialMessageViz.innerHTML = errorMsg;
                    initialMessageViz.style.display = 'flex';
                } else {
                    dataOutput.innerHTML = errorMsg;
                }
            }
        } finally {
            currentApiController = null;
        }
    }

    // --- MODIFIED: Modal Logic to show real data ---
    async function fetchAndShowDetails(noradId, satName) {
        modalTitle.textContent = `Details for ${satName}`;
        modalBody.innerHTML = '<p class="text-yellow-400">Fetching mission data...</p>';
        detailsModal.classList.remove('hidden');

        try {
            const response = await fetch(`${SERVER_URL}/details`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ email: user.email, token: token, norad_id: parseInt(noradId) })
            });

            if (!response.ok) {
                const errData = await response.json();
                throw new Error(errData.error || `Server error: ${response.status}`);
            }
            const data = await response.json();

            modalBody.innerHTML = `
                <p><strong class="text-gray-400 w-32 inline-block">NORAD ID:</strong> ${noradId}</p>
                <p><strong class="text-gray-400 w-32 inline-block">Official Name:</strong> ${data.official_name}</p>
                <p><strong class="text-gray-400 w-32 inline-block">Launch Date:</strong> ${data.launch_date}</p>
                <p><strong class="text-gray-400 w-32 inline-block">Country:</strong> ${data.country}</p>
                <p><strong class="text-gray-400 w-32 inline-block">Mission:</strong> ${data.purpose}</p>
                <p><strong class="text-gray-400 w-32 inline-block">Status:</strong> <span class="text-green-400">${data.status}</span></p>
            `;

        } catch (error) {
             modalBody.innerHTML = `<p class="text-red-400">Error: Could not fetch details. ${error.message}</p>`;
        }
    }

    // NEW: Event listener for satellite links in data output
    dataOutput.addEventListener('click', (e) => {
        if (e.target.classList.contains('satellite-link')) {
            e.preventDefault();
            const noradId = e.target.getAttribute('data-norad');
            const satName = e.target.getAttribute('data-name');
            fetchAndShowDetails(noradId, satName);
        }
    });

    // NEW: Event listeners to close the modal
    modalCloseBtn.addEventListener('click', () => detailsModal.classList.add('hidden'));
    detailsModal.addEventListener('click', (e) => {
        if (e.target === detailsModal) { // Only close if clicking on the overlay itself
            detailsModal.classList.add('hidden');
        }
    });


    // --- EVENT LISTENERS ---
    document.getElementById('btnList').addEventListener('click', () => fetchAPI('/list', {}, 'List All Satellites'));
    document.getElementById('btnFilter').addEventListener('click', () => {
        const min_alt = parseFloat(document.getElementById('min_alt').value);
        const max_alt = parseFloat(document.getElementById('max_alt').value);
        fetchAPI('/filter', { min_alt, max_alt }, `Filter: ${min_alt}-${max_alt}km`);
    });
    document.getElementById('btnRisk_dash').addEventListener('click', () => {
        const target_alt = parseFloat(document.getElementById('target_alt_risk_dash').value);
        const tolerance = parseFloat(document.getElementById('tolerance_dash').value);
        document.getElementById('satellitesTab').click();
        setTimeout(() => {
            fetchAPI('/risk', { target_alt, tolerance }, "Collision Risk Check");
        }, 50);
    });
    document.getElementById('btnPredict').addEventListener('click', () => {
        const duration = parseInt(document.getElementById('duration').value);
        const step = parseInt(document.getElementById('step').value);
        const threshold = parseFloat(document.getElementById('threshold').value);
        fetchAPI('/predict', { duration, step, threshold }, "Collision Prediction");
    });
    document.getElementById('btnPlan').addEventListener('click', () => {
        const target_alt = parseFloat(document.getElementById('target_alt_plan').value);
        fetchAPI('/plan', { target_alt }, "Safe Path Planner");
    });

    document.getElementById('btnGetPro').addEventListener('click', async () => {
        const upgradeMsg = document.getElementById('upgrade-message');
        upgradeMsg.textContent = "Upgrading...";
        try {
            const response = await fetch(`${SERVER_URL}/upgrade`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ email: user.email, token: token })
            });
            const data = await response.json();
            if (!response.ok) throw new Error(data.error || "Upgrade failed.");
            localStorage.setItem('orbitGuardUser', JSON.stringify(data.user));
            upgradeMsg.textContent = 'Upgrade successful! All Pro features are now unlocked.';
            applyFeatureGating();
        } catch (error) {
            upgradeMsg.textContent = `Error: ${error.message}`;
        }
    });

    document.getElementById('btnGetFree').addEventListener('click', () => {
         alert("Functionality to downgrade to the free plan is not yet implemented in the backend.");
    });
    
    document.getElementById('btnGenerateKey').addEventListener('click', async (event) => {
        event.preventDefault();

        const keyDisplay = document.getElementById('api-key-display');
        const keyText = document.getElementById('apiKeyText');
        const genButton = document.getElementById('btnGenerateKey');

        keyText.textContent = 'Generating...';
        keyDisplay.classList.remove('hidden');
        genButton.disabled = true;

        const oldNotice = keyDisplay.querySelector('.api-key-notice');
        if (oldNotice) oldNotice.remove();

        try {
            const response = await fetch(`${SERVER_URL}/generate-key`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ email: user.email, token: token })
            });
            const data = await response.json();
            if (!response.ok) throw new Error(data.error || "Key generation failed.");
            
            keyText.textContent = data.api_key;
            keyDisplay.classList.remove('hidden');
            genButton.textContent = 'Key Generated';
            genButton.disabled = true;
            
            const notice = document.createElement('p');
            notice.className = 'text-yellow-400 text-sm mt-4 api-key-notice';
            notice.textContent = 'Your new key is valid for 24 hours. Please copy it now.';
            keyDisplay.appendChild(notice);

        } catch (error) {
            keyText.textContent = `Error generating key: ${error.message}`;
            keyDisplay.classList.remove('hidden');
            genButton.disabled = false;
            genButton.textContent = 'Generate';
        }
    });
});