document.addEventListener('DOMContentLoaded', () => {
    const SERVER_URL = 'http://localhost:8080';
    const loginForm = document.getElementById('login-form');
    const signupForm = document.getElementById('signup-form');
    const errorMessage = document.getElementById('error-message');

    const handleAuth = async (endpoint, body) => {
        try {
            const response = await fetch(`${SERVER_URL}${endpoint}`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(body)
            });

            const data = await response.json();

            if (!response.ok) {
                throw new Error(data.error || `HTTP error! Status: ${response.status}`);
            }

            if (data.token) {
                localStorage.setItem('orbitGuardToken', data.token);
                localStorage.setItem('orbitGuardUser', JSON.stringify(data.user));
                window.location.href = 'index.html'; // Redirect to main app
            } else {
                 errorMessage.textContent = data.error || 'An unknown error occurred.';
            }

        } catch (error) {
            errorMessage.textContent = `Error: ${error.message}. Is the C server running?`;
        }
    };

    if (loginForm) {
        loginForm.addEventListener('submit', (e) => {
            e.preventDefault();
            const email = document.getElementById('email').value;
            const password = document.getElementById('password').value;
            handleAuth('/login', { email, password });
        });
    }

    if (signupForm) {
        signupForm.addEventListener('submit', (e) => {
            e.preventDefault();
            const email = document.getElementById('email').value;
            const password = document.getElementById('password').value;
            handleAuth('/signup', { email, password });
        });
    }
});