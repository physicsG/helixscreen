// SPDX-License-Identifier: GPL-3.0-or-later
import { ref, watch, type Ref } from 'vue'
import { ApiError } from '@/services/api'

/**
 * Loads one resource and reloads it when `key` changes. A 404 sets `missing`
 * instead of `error`; a response that arrives after a newer load started is
 * dropped, and the previous data stays until the new load lands.
 */
export function useLoaded<T, K = null>(load: (key: K) => Promise<T>, key: () => K = () => null as K) {
  const data = ref(null) as Ref<T | null>
  const loading = ref(true)
  const error = ref('')
  const missing = ref(false)
  let latest = 0

  watch(
    key,
    async (k) => {
      const ticket = ++latest
      loading.value = true
      error.value = ''
      missing.value = false
      try {
        const result = await load(k)
        if (ticket === latest) data.value = result
      } catch (e) {
        if (ticket !== latest) return
        if (e instanceof ApiError && e.status === 404) missing.value = true
        else error.value = e instanceof Error ? e.message : 'Failed to load data'
      } finally {
        if (ticket === latest) loading.value = false
      }
    },
    { immediate: true }
  )

  return { data, loading, error, missing }
}
