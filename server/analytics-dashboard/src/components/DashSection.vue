<template>
  <section class="dash-section">
    <h2 class="section-title">{{ title }}</h2>
    <div v-if="loading" class="loading">Loading...</div>
    <div v-else-if="error || renderError" class="error">{{ error || renderError }}</div>
    <RenderBoundary v-else><slot /></RenderBoundary>
  </section>
</template>

<script setup lang="ts">
import { onErrorCaptured, ref, watch } from 'vue'
import RenderBoundary from './RenderBoundary.vue'
import { isRenderError } from '../utils/renderError'

const props = defineProps<{
  title: string
  loading?: boolean
  error?: string
}>()

// A section whose content throws while rendering shows the message in place of
// its content; the rest of the page keeps rendering. A new load clears it.
// Errors from click handlers, watchers and lifecycle hooks leave the content
// in place and propagate to the app's handler.
const renderError = ref('')
watch(
  () => props.loading,
  (loading) => {
    if (loading) renderError.value = ''
  }
)
onErrorCaptured((err, _instance, info) => {
  if (!isRenderError(info)) return
  renderError.value = `Failed to render: ${err instanceof Error ? err.message : String(err)}`
  return false
})
</script>
