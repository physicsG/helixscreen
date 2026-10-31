import { defineStore } from 'pinia'
import { computed, ref } from 'vue'
import type { LocationQuery, Router } from 'vue-router'
import { planActivation } from '@/utils/drill'
import type { Grouped } from '@/utils/versions'
import {
  applyDrill,
  applyDrills,
  buildDashQuery,
  parseDashQuery,
  sameDashQuery,
  type DashFilterKey,
  type DashQuery
} from '@/utils/urlState'

export const useFiltersStore = defineStore('filters', () => {
  const range = ref('30d')
  const platform = ref<string[]>([])
  const version = ref<string[]>([])
  const model = ref<string[]>([])
  const board = ref<string[]>([])
  const expand = ref<string[]>([])

  const filterRefs: Record<DashFilterKey, typeof platform> = { platform, version, model, board }

  const state = computed<DashQuery>(() => ({
    range: range.value,
    filters: {
      platform: platform.value,
      version: version.value,
      model: model.value,
      board: board.value
    },
    expand: expand.value
  }))

  // The query sent to the worker: expansion state is view-only, so it stays out.
  const queryParams = computed(() => buildDashQuery({ ...state.value, expand: [] }))

  const queryString = computed(() => new URLSearchParams(queryParams.value).toString())

  function apply(s: DashQuery) {
    range.value = s.range
    for (const key of Object.keys(filterRefs) as DashFilterKey[]) {
      filterRefs[key].value = s.filters[key]
    }
    expand.value = s.expand
  }

  function setRange(r: string) {
    range.value = r
  }

  function setPlatform(vals: string[]) {
    platform.value = vals
  }

  function setVersion(vals: string[]) {
    version.value = vals
  }

  function setModel(vals: string[]) {
    model.value = vals
  }

  function setBoard(vals: string[]) {
    board.value = vals
  }

  function clearFilters() {
    platform.value = []
    version.value = []
    model.value = []
    board.value = []
  }

  // A no-op when the route already describes the store, which breaks the
  // route -> store -> route loop.
  function syncFromRoute(query: LocationQuery) {
    const parsed = parseDashQuery(query)
    if (!sameDashQuery(parsed, state.value)) apply(parsed)
  }

  function syncToRoute(router: Router) {
    const query = buildDashQuery(state.value)
    if (sameDashQuery(parseDashQuery(router.currentRoute.value.query), state.value)) return
    return router.replace({ query })
  }

  // Pushes without touching the store: the route watcher feeds it back, so the
  // back button walks the drill path.
  function drill(router: Router, key: DashFilterKey, members: string[]) {
    const next = applyDrill(state.value, key, members)
    return router.push({ query: buildDashQuery(next) })
  }

  /**
   * The click handler for every drillable row, cell and card: expands or
   * collapses a group, or sets each drillable row's filter in one navigation.
   */
  function activate(router: Router, entries: [DashFilterKey, Grouped][]) {
    const plan = planActivation(entries)
    if (plan?.kind === 'expand') toggleExpand(plan.key)
    else if (plan?.kind === 'drill') return router.push({ query: buildDashQuery(applyDrills(state.value, plan.filters)) })
  }

  /**
   * Opens another tab with the current filters, each drillable entry applied on
   * top; `hash` names an element on the target page to scroll to.
   */
  function go(router: Router, path: string, entries: [DashFilterKey, Grouped][] = [], hash = '') {
    const plan = planActivation(entries)
    const next = plan?.kind === 'drill' ? applyDrills(state.value, plan.filters) : state.value
    return router.push({ path, query: buildDashQuery({ ...next, expand: [] }), hash })
  }

  function toggleExpand(groupKey: string) {
    const i = expand.value.indexOf(groupKey)
    expand.value =
      i >= 0 ? expand.value.filter((k) => k !== groupKey) : [...expand.value, groupKey]
  }

  function remove(key: DashFilterKey, value: string) {
    filterRefs[key].value = filterRefs[key].value.filter((v) => v !== value)
  }

  return {
    range,
    platform,
    version,
    model,
    board,
    expand,
    queryParams,
    queryString,
    setRange,
    setPlatform,
    setVersion,
    setModel,
    setBoard,
    clearFilters,
    syncFromRoute,
    syncToRoute,
    drill,
    activate,
    go,
    toggleExpand,
    remove
  }
})
