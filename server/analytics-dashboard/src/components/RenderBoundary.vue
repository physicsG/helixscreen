<template>
  <slot />
</template>

<script setup lang="ts">
// Renders its slot inside its own render, so an exception there reaches the
// parent's onErrorCaptured instead of breaking the parent's render.
</script>
