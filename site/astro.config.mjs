import { defineConfig } from 'astro/config';

export default defineConfig({
  output: 'static',
  trailingSlash: 'always',
  site: process.env.SITE_ORIGIN,
  base: process.env.SITE_BASE || '/',
  build: { format: 'directory' },
});
