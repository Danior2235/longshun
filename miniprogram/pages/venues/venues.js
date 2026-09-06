// 会场报价：按楼层分组展示 23 个会场门市价
const api = require('../../utils/api')

Page({
  data: {
    keyword: '',
    groups: [],
    total: 0,
    loading: false
  },

  onShow() {
    this.load()
  },

  load() {
    this.setData({ loading: true })
    return api
      .getVenues()
      .then((list) => {
        const kw = this.data.keyword.trim()
        const filtered = kw
          ? list.filter(
              (v) =>
                (v.name || '').indexOf(kw) >= 0 ||
                (v.area || '').indexOf(kw) >= 0 ||
                (v.notes || '').indexOf(kw) >= 0
            )
          : list
        const map = {}
        filtered.forEach((v) => {
          const area = v.area || '其他'
          if (!map[area]) map[area] = []
          map[area].push(this.decorate(v))
        })
        const groups = Object.keys(map).map((name) => ({ name, items: map[name] }))
        this.setData({ groups, total: filtered.length, loading: false })
      })
      .catch((err) => {
        this.setData({ loading: false })
        wx.showToast({ title: '加载失败，请检查服务端', icon: 'none' })
        console.error(err)
      })
  },

  decorate(v) {
    const eq = v.equipment || {}
    const layout = v.layout || {}
    const caps = []
    if (layout.theater) caps.push('剧院' + layout.theater)
    if (layout.classroom) caps.push('课桌' + layout.classroom)
    if (layout.island) caps.push('岛屿' + layout.island)
    if (layout.round_table) caps.push('圆桌' + layout.round_table)
    const eqTags = []
    if (eq.has_led) eqTags.push(eq.led_size ? 'LED ' + eq.led_size : 'LED')
    if (eq.has_sound) eqTags.push('音响')
    if (eq.has_projector) eqTags.push('投影')
    if (eq.mic_count > 0) eqTags.push('话筒' + eq.mic_count + '支')
    if (eq.has_tea) eqTags.push('茶水')
    if (eq.screen_fee > 0) eqTags.push('大屏+' + eq.screen_fee + '元/天')
    const rules = v.rules || {}
    const ruleTags = []
    if (rules.is_empty_venue) ruleTags.push('空厅')
    if (rules.min_tables > 0) ruleTags.push('餐' + rules.min_tables + '桌起订')
    if (rules.min_catering_free_venue > 0) ruleTags.push('餐满' + rules.min_catering_free_venue + '免场地')
    if (rules.rehearsal_included) ruleTags.push('含彩排')
    if (rules.lighting_extra_fee > 0) ruleTags.push('灯光+' + rules.lighting_extra_fee)
    return {
      ...v,
      cap_text:
        v.capacity_min || v.capacity_max
          ? v.capacity_min + '-' + v.capacity_max + '人'
          : '—',
      price_text: v.price_by_hour
        ? '¥' + v.price_hourly + '/小时'
        : '半天 ¥' + v.price_half_day + ' / 全天 ¥' + v.price_full_day,
      layout_text: caps.join(' · ') || '待定',
      eqTags,
      ruleTags
    }
  },

  onSearch(e) {
    this.setData({ keyword: e.detail.value }, () => this.load())
  },

  onBook(e) {
    const v = e.currentTarget.dataset.item
    const amount = v.price_by_hour ? v.price_hourly : v.price_half_day
    wx.navigateTo({
      url:
        '/pages/booking/booking?type=venue&item=' +
        encodeURIComponent(v.name) +
        '&amount=' +
        amount
    })
  }
})
