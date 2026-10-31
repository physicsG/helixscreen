<template>
  <DashSection title="Release scorecard" :loading="loading" :error="error">
    <ChartCaption
      :text="`Per version, ${span}: rates are sample-weighted; crash-free is the share of active devices with no crash`"
    />
    <ChartCaption v-if="note" :text="note" />
    <NoData v-if="rows.length === 0" />
    <div v-else class="table-wrapper">
      <table class="data-table scorecard">
        <thead>
          <tr>
            <th>Version</th>
            <th>Active devices</th>
            <th>Crash-free devices</th>
            <th>Crash rate</th>
            <th>Update failure</th>
            <th>Print success</th>
            <th>p95 frame</th>
            <th>p95 RSS</th>
            <th>Sessions</th>
            <th>Crashes</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(ver, i) in rows" :key="ver.version">
            <td>
              <span class="swatch" :style="{ background: seriesColor(i) }"></span>
              <span class="version-header">{{ ver.version }}</span>
            </td>
            <td class="mono">{{ ver.active_devices.toLocaleString() }}</td>
            <td class="mono">{{ formatRate(ver.crash_free_devices) }}</td>
            <td class="mono" :class="crashRateClass(ver.crash_rate)">
              {{ formatRate(ver.crash_rate) }}
              <DeltaTag :change="changeFromOlder(i, 'crash_rate', false)" />
            </td>
            <td class="mono">
              {{ formatRateOf(ver.update_failure_rate, ver.update_failures, ver.update_failures + ver.update_successes) }}
            </td>
            <td class="mono" :class="successRateClass(ver.print_success_rate)">
              {{ formatRate(ver.print_success_rate) }}
              <DeltaTag :change="changeFromOlder(i, 'print_success_rate', true)" />
            </td>
            <td class="mono">{{ ver.p95_frame_ms === null ? 'n/a' : `${ver.p95_frame_ms.toFixed(1)} ms` }}</td>
            <td class="mono">{{ ver.p95_rss_kb === null ? 'n/a' : formatMB(ver.p95_rss_kb) }}</td>
            <td class="mono">{{ ver.total_sessions.toLocaleString() }}</td>
            <td class="mono crashes">{{ notApplicable?.crashes ? 'n/a' : ver.total_crashes.toLocaleString() }}</td>
          </tr>
        </tbody>
      </table>
    </div>

    <div class="version-selector">
      <h3>
        Versions shown
        <span class="hint">{{ selected === null ? 'newest with traffic in the range' : `${selected.length} picked` }}</span>
        <button v-if="selected !== null" class="reset" @click="emit('reset')">Show newest</button>
      </h3>
      <div class="version-pills">
        <template v-for="chip in chips" :key="chip.key">
          <button
            v-if="chip.expandable"
            class="version-pill group"
            :class="{ open: chip.expanded, 'has-selection': !chip.expanded && chip.shown > 0 }"
            :title="rowHint(chip)"
            @click="filters.toggleExpand(chip.key)"
          >
            {{ rowLabel(chip) }}<span v-if="!chip.expanded && chip.shown > 0" class="group-shown">{{ chip.shown }} shown</span>
          </button>
          <button
            v-else
            class="version-pill"
            :class="{ active: chip.shown > 0, disabled: atLimit(chip) }"
            :disabled="atLimit(chip)"
            @click="emit('toggle', chip.key)"
          >
            {{ chip.label }}
          </button>
        </template>
      </div>
    </div>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import DashSection from '@/components/DashSection.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import DeltaTag from '@/components/DeltaTag.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import type { ReleasesData } from '@/services/api'
import { useFiltersStore } from '@/stores/filters'
import { rowHint, rowLabel } from '@/utils/drill'
import { formatMB, formatRate, formatRateOf, ppChange, unreportedNote } from '@/utils/format'
import { seriesColor } from '@/utils/series'
import { versionChips, type Grouped } from '@/utils/versions'

type Release = ReleasesData['versions'][number]

/** The worker compares at most this many versions in one request. */
const MAX_VERSIONS = 20

const props = defineProps<{
  /** Rows newest first; row i is drawn in seriesColor(i) on every chart of the tab. */
  rows: Release[]
  loading: boolean
  error: string
  /** Versions picked by hand, or null for the worker's default set. */
  selected: string[] | null
  /** Per block, the active filter its rows do not carry; null before the first load. */
  notApplicable: ReleasesData['not_applicable'] | null
}>()

const emit = defineEmits<{ toggle: [version: string]; reset: [] }>()

const { data: adoption, span } = useDashData(api.getAdoption)
const filters = useFiltersStore()

const shown = computed(() => new Set(props.rows.map((r) => r.version)))

// Older lines and unreleased betas collapse to one chip that opens in place,
// as on the version charts; the open groups live in the URL.
const chips = computed(() =>
  versionChips(adoption.value?.versions ?? [], new Set(filters.expand), shown.value)
)

/** A version not shown cannot be added once the worker's limit is reached. */
function atLimit(chip: Grouped & { shown: number }): boolean {
  return chip.shown === 0 && shown.value.size >= MAX_VERSIONS
}

const note = computed(() => {
  const na = props.notApplicable
  if (!na) return ''
  return unreportedNote([
    ['Crash-free devices', na.crashes],
    ['Crash rate', na.crashes],
    ['Crashes', na.crashes],
    ['Update failure', na.updates],
    ['Print success', na.prints],
    ['p95 frame', na.performance],
    ['p95 RSS', na.performance]
  ])
})

// Change from the next older version in the table.
function changeFromOlder(i: number, key: 'crash_rate' | 'print_success_rate', upIsGood: boolean) {
  const older = props.rows[i + 1]
  return older ? ppChange(props.rows[i][key], older[key], upIsGood) : null
}

function crashRateClass(rate: number | null): string {
  if (rate === null) return ''
  const pct = rate * 100
  if (pct > 10) return 'rate-bad'
  if (pct > 5) return 'rate-warn'
  return 'rate-good'
}

function successRateClass(rate: number | null): string {
  if (rate === null) return ''
  const pct = rate * 100
  if (pct >= 80) return 'rate-good'
  if (pct >= 60) return 'rate-warn'
  return 'rate-bad'
}
</script>

<style scoped>
.swatch {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 2px;
  margin-right: 8px;
}

.version-header {
  font-weight: 700;
  color: var(--accent-blue);
}

.scorecard td {
  vertical-align: top;
}

.crashes {
  color: var(--accent-red);
}

.rate-good {
  color: var(--accent-green);
}

.rate-warn {
  color: var(--accent-yellow);
}

.rate-bad {
  color: var(--accent-red);
}

.version-selector {
  margin-top: 16px;
  margin-bottom: 24px;
}

.version-selector h3 {
  font-size: 14px;
  font-weight: 500;
  color: var(--text-secondary);
  margin-bottom: 12px;
}

.hint {
  font-weight: 400;
  font-size: 12px;
  margin-left: 8px;
}

.reset {
  margin-left: 12px;
  padding: 2px 10px;
  background: transparent;
  border: 1px solid var(--border);
  border-radius: 10px;
  color: var(--text-secondary);
  font-size: 12px;
}

.version-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.version-pill {
  padding: 6px 14px;
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: 20px;
  color: var(--text-secondary);
  font-size: 13px;
  cursor: pointer;
  transition: all 0.15s;
}

.version-pill:hover:not(.disabled) {
  border-color: var(--accent-blue);
  color: var(--text-primary);
}

.version-pill.active {
  background: rgba(59, 130, 246, 0.15);
  border-color: var(--accent-blue);
  color: var(--accent-blue);
}

.version-pill.group {
  border-style: dashed;
}

.version-pill.group.open {
  border-style: solid;
  color: var(--text-primary);
}

.version-pill.has-selection {
  border-style: solid;
  border-color: var(--accent-blue);
  color: var(--accent-blue);
}

.group-shown {
  margin-left: 6px;
  padding: 0 6px;
  border-radius: 8px;
  background: rgba(59, 130, 246, 0.15);
  font-size: 11px;
}

.version-pill.disabled {
  opacity: 0.35;
  cursor: not-allowed;
}
</style>
