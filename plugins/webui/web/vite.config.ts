import tailwindcss from '@tailwindcss/vite';
import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig } from 'vite';

export default defineConfig(
  {
    plugins: [tailwindcss(), sveltekit()],
    content: [
      './src/**/*.{html,js,svelte,ts}',
      './node_modules/sverminal/**/*.{html,svelte,js,ts}'
    ]
  }
);
// Vim: set expandtab tabstop=2 shiftwidth=2:
