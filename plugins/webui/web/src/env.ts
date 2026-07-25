import { defineEnvVars } from '@sveltejs/kit/hooks';
import * as v from 'valibot';

export const variables = defineEnvVars({
  API_HOST: {
    public: true,
    static: true,
    schema: v.string()
  },
});
